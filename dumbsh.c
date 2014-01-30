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
  struct double_list *current;
)

#define TT this.dumbsh

static void updateLine()
{
  if (0 > TT.x)  TT.x = 0;
  if (strlen(toybuf) <= TT.x)  TT.x = strlen(toybuf);
  if (TT.w < TT.x)  TT.x = TT.w;
  if (0 > TT.y)  TT.y = 0;
  if (TT.h < TT.y)
  {
    printf("\x1B[%d;0H\n", TT.y + 1);
    fflush(stdout);
    TT.y = TT.h;
  }
  printf("\x1B[%d;0H%-*s\x1B[%d;%dH", TT.y + 1, TT.w, toybuf, TT.y + 1, TT.x + 1);
  fflush(stdout);
}

// Callback for incoming CSI commands from the terminal.
static void handleCSI(long extra, char *command, int *params, int count)
{
  // Is it a cursor location report?
  if (strcmp("R", command) == 0)
  {
    // Parameters are cursor line and column.  Note this may be sent at other times, not just during terminal resize.
    // The defaults are 1, which get ignored by the heuristic below.
    int r = params[0], c = params[1];

    // Check it's not an F3 key variation, coz some of them use the same CSI function code.
    // This is a heuristic, we are checking against an unusable terminal size.
    if ((2 == count) && (8 < r) && (8 < c))
    {
      TT.h = r;
      TT.w = c;
      updateLine();
    }
  }
}

// The various commands.
static void deleteChar()
{
  int j;

  for (j = TT.x; toybuf[j]; j++)
    toybuf[j] = toybuf[j + 1];
  updateLine();
}

static void backSpaceChar()
{
  if (TT.x)
  {
    TT.x--;
    deleteChar();
  }
}

// This is where we would actually deal with what ever command the user had typed in.
// For now we just move on.
static void doCommand()
{
  toybuf[0] = 0;
  TT.x = 0;
  TT.y++;
  updateLine();
}

static void endOfLine()
{
  TT.x = strlen(toybuf) - 1;
  updateLine();
}

static void leftChar()
{
  TT.x--;
  updateLine();
}

static void nextHistory()
{
  TT.current = TT.current->next;
  strcpy(toybuf, TT.current->data);
  TT.x = strlen(toybuf);
  updateLine();
}

static void prevHistory()
{
  TT.current = TT.current->prev;
  strcpy(toybuf, TT.current->data);
  TT.x = strlen(toybuf);
  updateLine();
}

static void quit()
{
  handle_keys_quit();
}

static void rightChar()
{
  TT.x++;
  updateLine();
}

static void startOfLine()
{
  TT.x = 0;
  updateLine();
}

// The key to command mappings, Emacs style.
static struct keyCommand simpleEmacsKeys[] =
{
  {"BS",	backSpaceChar},
  {"Del",	deleteChar},
  {"^D",	deleteChar},
  {"Return",	doCommand},
  {"^J",	doCommand},
  {"^M",	doCommand},
  {"Down",	nextHistory},
  {"^N",	nextHistory},
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
  {"Up",	prevHistory},
  {"^P",	prevHistory},
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

  // See if it's ordinary keys.
  if (isprint(sequence[0]))
  {
    if (TT.x < sizeof(toybuf))
    {
      int j, l = strlen(sequence);

      for (j = strlen(toybuf); j >= TT.x; j--)
        toybuf[j + l] = toybuf[j];
      for (j = 0; j < l; j++)
        toybuf[TT.x + j] = sequence[j];
      TT.x += l;
      updateLine();
    }
    return 1;
  }

  return 0;
}

void dumbsh_main(void)
{
  struct termios termio, oldtermio;
  char *temp = getenv("HOME");
  int fd;

  // Load bash history.
  temp = xmsprintf("%s/%s", temp ? temp : "", ".bash_history");
  if (-1 != (fd = open(temp, O_RDONLY)))
  {
    while ((temp = get_line(fd)))  TT.current = dlist_add(&TT.current, temp);
    close(fd);
  }
  if (!TT.current)
    TT.current = dlist_add(&TT.current, "");

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

  // Let the mouldy old terminal mold us.
  TT.w = 80;
  TT.h = 24;
  terminal_size(&TT.w, &TT.h);

  updateLine();
  handle_keys(0, handleKeySequence, handleCSI);

  tcsetattr(0, TCSANOW, &oldtermio);
  puts("");
  fflush(stdout);
}
