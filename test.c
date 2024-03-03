jjj


/* includes */



#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

#include#include <ctype.h>
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
	my_code
}
