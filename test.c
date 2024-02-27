/* includes */
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
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

enum editorKey {
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
