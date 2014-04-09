/* showkey.c - Shows the keys pressed.
 *
 * Copyright 2014 David Seikel <won_fang@yahoo.com.au>
 *
 * Dunno yet if this is a standard.

USE_SHOWKEY(NEWTOY(showkey, "", TOYFLAG_USR|TOYFLAG_BIN))

config SHOWKEY
  bool "showkey"
  default n
  help
    usage: showkey

    Shows the keys pressed.
*/

#include "toys.h"
#include "lib/handlekeys.h"

typedef void (*eventHandler) (void);

struct keyCommand
{
  char *key;
  eventHandler handler;
};

GLOBALS(
  unsigned h, w;
  int x, y;
)

#define TT this.showkey


// Callback for incoming CSI commands from the terminal.
static void handleCSI(long extra, char *command, int *params, int count)
{
  int i;

  // Is it a cursor location report?
  if (strcmp("R", command) == 0)
  {
    printf("CSI cursor position - line %d, column %d\r\n", params[0], params[1]);
    return;
  }

  printf("CSI command %s - ", command);
  for (i = 0; i < count; i++)
    printf("%d ", params[i]);
  printf("\r\n");
}

static void quit()
{
  printf("Quitting.\r\n");
  handle_keys_quit();
}

// The key to command mappings.
static struct keyCommand simpleKeys[] =
{
  {"^C",	quit}
};

// Callback for incoming key sequences from the user.
static int handleKeySequence(long extra, char *sequence, int isTranslated)
{
  int j, l = strlen(sequence);

  if (isTranslated)
    printf("TRANSLATED - ");
  else
    printf("KEY - ");
  printf("%s\r\n", sequence);

  // Search for a key sequence bound to a command.
  for (j = 0; j < (sizeof(simpleKeys) / sizeof(*simpleKeys)); j++)
  {
    if (strncmp(simpleKeys[j].key, sequence, l) == 0)
    {
      // If it's a partial match, keep accumulating them.
      if (strlen(simpleKeys[j].key) != l)
        return 0;
      else
      {
        if (simpleKeys[j].handler)  simpleKeys[j].handler();
        return 1;
      }
    }
  }

  return 1;
}

void showkey_main(void)
{
  struct termios termIo, oldTermIo;

  // Grab the old terminal settings and save it.
  tcgetattr(0, &oldTermIo);
  tcflush(0, TCIFLUSH);
  termIo = oldTermIo;

  // Mould the terminal to our will.
  // In this example we are turning off all the terminal smarts, but real code
  // might not want that.
  termIo.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL
                     | IUCLC | IXON | IXOFF | IXANY);
  termIo.c_oflag &= ~OPOST;
  termIo.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | TOSTOP | ICANON | ISIG
                     | IEXTEN);
  termIo.c_cflag &= ~(CSIZE | PARENB);
  termIo.c_cflag |= CS8;
  termIo.c_cc[VTIME]=0;  // deciseconds.
  termIo.c_cc[VMIN]=1;
  tcsetattr(0, TCSANOW, &termIo);

  handle_keys(0, handleKeySequence, handleCSI);

  tcsetattr(0, TCSANOW, &oldTermIo);
  puts("");
  fflush(stdout);
}
