/* includes */
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>



/*** defines ***/ 
#define ED_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
#define ED_TAB_WIDTH 8
#define ED_QUIT_TIMES 1

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY,
};


/*** data ***/

typedef struct editor_row {
  int size;
  char *chars;
  int rsize;
  char *render;
} editor_row;

struct editorConfig {
  struct termios orig_termios;
  int screenrows;
  int screencols;
  int rowoff;
  int coloff;
  int cx, cy;
  int rx;
  int numrows;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  editor_row *row;
  int dirty;
};

struct editorConfig E;

/* prototypes */
void editorSetStatusMessage(const char *fmt, ...);
void editorUpdateRowRender(editor_row*);
void editorRefreshScreen();
char *editorPrompt(char *prompt);

/*** terminal ***/

void die(const char *s) {
  // Clear the screen in case of any error
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)  {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  // 
  raw.c_iflag &= ~(BRKINT | IXON | ICRNL | INPCK | ISTRIP);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cflag |= (CS8);
  raw.c_oflag &= ~(OPOST);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while(i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

// notice how we've programmed gWS to return 0 on success and -1 on failure
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0) {
    // we needed to do this because all pltforms may not support TIOCGWINSZ
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

// row operations

void editorRowDelChar(editor_row* row, int at) {
  if (at < 0 || at > row->size) return;
  memmove(&row->chars[at], &row->chars[at+1], row->size - at);
  row->size--;
  editorUpdateRowRender(row);
  E.dirty++;
}

int editorRowCxToRx(editor_row *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += ED_TAB_WIDTH - 1;
    rx++;
  }
  return rx;
}

void editorUpdateRowRender(editor_row *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (ED_TAB_WIDTH - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % ED_TAB_WIDTH != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(editor_row) * (E.numrows + 1));
  memmove(&E.row[at+1], &E.row[at], sizeof(editor_row) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len+1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRowRender(&E.row[at]);
  E.numrows++;
  E.dirty++;
}

void editorFreeRow(editor_row *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at+1], sizeof(editor_row) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(editor_row *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
  row->chars[at] = c;
  if (at == row->size) {
    row->chars[++at] = '\0';
  }
  row->size++;
  editorUpdateRowRender(row);
  E.dirty++;
}

void editorRowAppendString(editor_row *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRowRender(row);
  E.dirty++;
}

/* editor operations */
void editorInsertChar(int c) {
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() {
  editor_row* row = &E.row[E.cy];
  char *newline = strdup(&row->chars[E.cx]);
  row->size = E.cx;
  row->chars[E.cx] = '\0';
  editorUpdateRowRender(row);
  E.cy++;
  E.cx = 0;
  editorInsertRow(E.cy, newline, strlen(newline));
}

void editorDelChar() {
  if (E.cx == 0 && E.cy==0) return;

  editor_row* row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/* file io */

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s");
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/** append buffer for better performance as individual writes is expensive **/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}


/* input */ 
char *editorPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while(1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

void editorMoveCursor(int key) {
  editor_row *row = (E.cy >= E.numrows)? NULL: &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) E.cx--;
      else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row) {
        if (E.cx < row->size) {
          E.cx++;
        } else {
          if (E.cy < E.numrows - 1) {
            E.cy++;
            E.cx = 0;
          }
        }
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) E.cy--;
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows - 1) {
        E.cy++;
      }
      break;
  }
  row = (E.cy >= E.numrows)? NULL: &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times = ED_QUIT_TIMES;
  int c = editorReadKey();

  switch(c) {
    case '\r':
      editorInsertNewline();
      break;
    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0){
        editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;
    case CTRL_KEY('s'):
      editorSave();
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    case CTRL_KEY('l'):
    case '\x1b':
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }
        // move up or down one screen based on page up or down key
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP? ARROW_UP : ARROW_DOWN);
      }
      break;
    default:
      editorInsertChar(c);
      break;
  }
  quit_times = ED_QUIT_TIMES;
}


/* output */

// its name should be correct_coordinates();
void editorAdjustCoordinates() {
  E.rx = E.cx;

  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows ; y++) {
    int row_no = E.rowoff + y;
    if (row_no >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Edytor -- version %s", ED_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[row_no].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[row_no].render[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawMessagebar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status),"%.20s - %d lines %s", E.filename? E.filename: "[No Name]", E.numrows, E.dirty? "(modified)": "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    }
    abAppend(ab, " ", 1);
    len++;
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorRefreshScreen() {
  editorAdjustCoordinates();
  struct abuf ab = ABUF_INIT;

  // hide the cursor before painting and show after painting
  abAppend(&ab, "\x1b[?25l", 6);
  // move the cursor to the top left
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessagebar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/** init **/
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numrows = 0;
  E.row = NULL;
  E.rowoff = 0;
  E.coloff = 0;
  E.dirty = 0;
  E.rx = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();

  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save");
  while(1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
