/* dumbsh.c - A really dumb shell, to demonstrate handlekeys usage.
 *
 * Copyright 2014 David Seikel <won_fang@yahoo.com.au>
 *
 * Not a real shell, so doesn't follow any standards, 
 * coz it wont implement them anyway.

USE_DUMBSH(NEWTOY(dumbsh, "", TOYFLAG_USR|TOYFLAG_BIN))

config DUMBSH
  bool "dumbsh"
  default n
  help
    usage: dumbsh

    A really dumb shell.
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

#define TT this.dumbsh

static void moveCursor()
{
  if (0 > TT.y)  TT.y = 0;
  if (0 > TT.x)  TT.x = 0;
  if (strlen(toybuf) <= TT.x)  TT.x = strlen(toybuf);
  if (TT.w < TT.y)  TT.y = TT.w;
  if (TT.h < TT.x)  TT.x = TT.h;
  printf("\x1B[%d;0H%s\x1B[%d;%dH", TT.y + 1, toybuf, TT.y + 1, TT.x + 1);
  fflush(stdout);
}

static void handleCSI(long extra, char *command, int *params, int count)
{
  // Parameters are cursor line and column.  Note this may be sent at other times, not just during terminal resize.
  if (strcmp("R", command) == 0)
  {
    int r = params[0], c = params[1];

    // The defaults are 1, which get ignored by the heuristic below.
    // Check it's not an F3 key variation, coz some of them use the same CSI function code.
    // This is a heuristic, we are checking against an unusable terminal size.
    if ((2 == count) && (8 < r) && (8 < c))
    {
      TT.h = r;
      TT.w = c;
      moveCursor();
    }
  }
}

static void deleteChar()
{
  int j;

  for (j = TT.x; toybuf[j]; j++)
    toybuf[j] = toybuf[j + 1];
  moveCursor();
}

static void backSpaceChar()
{
  if (TT.x)
  {
    TT.x--;
    deleteChar();
  }
}

static void doCommand()
{
  // This is where we would actually deal with what ever command the user had typed in.
  // For now we just move on.
  toybuf[0] = 0;
  TT.x = 0;
  TT.y++;
  moveCursor();
}

static void downLine()
{
  // Do command history stuff here.
}

static void endOfLine()
{
  TT.x = strlen(toybuf) - 1;
  moveCursor();
}

static void leftChar()
{
  TT.x--;
  moveCursor();
}

static void quit()
{
  handle_keys_quit();
}

static void rightChar()
{
  TT.x++;
  moveCursor();
}

static void startOfLine()
{
  TT.x = 0;
  moveCursor();
}

static void upLine()
{
  // Do command history stuff here.
}

// The key to command mappings.
static struct keyCommand simpleEmacsKeys[] =
{
  {"Del",	backSpaceChar},
  {"^D",	deleteChar},
  {"Return",	doCommand},
  {"Down",	downLine},
  {"^N",	downLine},
  {"End",	endOfLine},
  {"^E",	endOfLine},
  {"Left",	leftChar},
  {"^B",	leftChar},
  {"^X^C",	quit},
  {"^C",	quit},
  {"Right",	rightChar},
  {"^F",	rightChar},
  {"Home",	startOfLine},
  {"^A",	startOfLine},
  {"Up",	upLine},
  {"^P",	upLine},
  {NULL, NULL}
};

static int handleKeySequence(long extra, char *sequence)
{
  int j;

  // Search for a key sequence bound to a command.
  for (j = 0; simpleEmacsKeys[j].key; j++)
  {
    if (strcmp(simpleEmacsKeys[j].key, sequence) == 0)
    {
      if (simpleEmacsKeys[j].handler)  simpleEmacsKeys[j].handler();
      return 1;
    }
  }

  if ((0 == sequence[1]) && isprint(sequence[0]))	// See if it's an ordinary key.
  {
    if (TT.x < sizeof(toybuf))
    {
      int j;

      for (j = strlen(toybuf); j >= TT.x; j--)
        toybuf[j + 1] = toybuf[j];
      toybuf[TT.x] = sequence[0];
      TT.x++;
      moveCursor();
    }
    return 1;
  }

  return 0;
}

void dumbsh_main(void)
{
  struct termios termio, oldtermio;

  // Grab the old terminal settings and save it.
  tcgetattr(0, &oldtermio);
  tcflush(0, TCIFLUSH);
  termio = oldtermio;

  // Mould the terminal to our will.
  termio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC | IXON | IXOFF | IXANY);
  termio.c_oflag &= ~OPOST;
  termio.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | TOSTOP | ICANON | ISIG | IEXTEN);
  termio.c_cflag &= ~(CSIZE | PARENB);
  termio.c_cflag |= CS8;
  termio.c_cc[VTIME]=0;  // deciseconds.
  termio.c_cc[VMIN]=1;
  tcsetattr(0, TCSANOW, &termio);

  TT.w = 80;
  TT.h = 24;
  terminal_size(&TT.w, &TT.h);

  // Run the main loop.
  handle_keys(0, handleKeySequence, handleCSI);

  // Restore the old terminal settings.
  tcsetattr(0, TCSANOW, &oldtermio);

  puts("");
  fflush(stdout);
}
