/* dumbsh.c - A really dumb shell, to demonstrate handle_keys usage.
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
  struct double_list *history;
)

#define TT this.dumbsh

// Sanity check cursor location and update the current line.
static void updateLine()
{
  if (0 > TT.x)  TT.x = 0;
  if (0 > TT.y)  TT.y = 0;
  if (TT.w < TT.x)  TT.x = TT.w;
  if (TT.h < TT.y)  TT.y = TT.h;
  if (strlen(toybuf) < TT.x)  TT.x = strlen(toybuf);
  printf("\x1B[%d;0H%-*s\x1B[%d;%dH",
    TT.y + 1, TT.w, toybuf, TT.y + 1, TT.x + 1);
  fflush(stdout);
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

// This is where we would actually deal with 
// what ever command the user had typed in.
// For now we just move on to the next line.
// TODO - We would want to redirect I/O, capture some keys (^C),
//        but pass the rest on.
//        Dunno yet how to deal with that.
//        We still want handle_keys to be doing it's thing,
//        so maybe handing it another fd, and a callback.
//        A function to add and remove fd and callback pairs for
//        handle_keys to check?
static void doCommand()
{
  toybuf[0] = 0;
  TT.x = 0;
  TT.y++;
  printf("\n");
  fflush(stdout);
  updateLine();
}

static void endOfLine()
{
  TT.x = strlen(toybuf);
  updateLine();
}

static void leftChar()
{
  TT.x--;
  updateLine();
}

static void nextHistory()
{
  TT.history = TT.history->next;
  strcpy(toybuf, TT.history->data);
  TT.x = strlen(toybuf);
  updateLine();
}

static void prevHistory()
{
  TT.history = TT.history->prev;
  strcpy(toybuf, TT.history->data);
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
static const struct keyCommand simpleEmacsKeys[] =
{
  {"BS",	backSpaceChar},
  {"Del",	deleteChar},
  {"^D",	deleteChar},
  {"Return",	doCommand},
  {"Enter",	doCommand},
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
  {"^P",	prevHistory}
};

// Callback for incoming sequences from the terminal.
static int handleEvent(long extra, struct keyevent *event)
{
  switch (event->type)
  {
    case HK_KEYS :
    {
      int j, l = strlen(event->sequence);

      // Search for a key sequence bound to a command.
      for (j = 0; j < ARRAY_LEN(simpleEmacsKeys); j++)
      {
        if (strncmp(simpleEmacsKeys[j].key, event->sequence, l) == 0)
        {
          // If it's a partial match, keep accumulating them.
          if (strlen(simpleEmacsKeys[j].key) != l)
            return 0;
          else
          {
            if (simpleEmacsKeys[j].handler)  simpleEmacsKeys[j].handler();
            return 1;
          }
        }
      }

      // See if it's ordinary keys.
      // NOTE - with vi style ordinary keys can be commands,
      // but they would be found by the command check above first.
      if (!event->isTranslated)
      {
        if (TT.x < sizeof(toybuf))
        {
          int j, l = strlen(event->sequence);

          for (j = strlen(toybuf); j >= TT.x; j--)
            toybuf[j + l] = toybuf[j];
          for (j = 0; j < l; j++)
            toybuf[TT.x + j] = event->sequence[j];
          TT.x += l;
          updateLine();
        }
      }
      break;
    }

    case HK_CSI :
    {
      // Is it a cursor location report?
      if (strcmp("R", event->sequence) == 0)
      {
        // Parameters are cursor line and column.
        // NOTE - This may be sent at other times, not just during terminal resize.
        //        We are assuming here that it's a resize.
        // The defaults are 1, which get ignored by the heuristic below.
        int r = event->params[0], c = event->params[1];

        // Check it's not an F3 key variation, coz some of them use 
        // the same CSI function command.
        // This is a heuristic, we are checking against an unusable terminal size.
        if ((2 == event->count) && (8 < r) && (8 < c))
        {
          TT.h = r;
          TT.w = c;
          updateLine();
        }
        break;
      }
    }

    default :  break;
  }

  // Tell handle_keys to drop it, coz we dealt with it, or it's not one of ours.
  return 1;
}

void dumbsh_main(void)
{
  struct termios termIo, oldTermIo;
  char *t = getenv("HOME");
  int fd;

  // Load bash history.
  t = xmprintf("%s/%s", t ? t : "", ".bash_history");
  if (-1 != (fd = open(t, O_RDONLY)))
  {
    while ((t = get_line(fd)))  TT.history = dlist_add(&TT.history, t);
    close(fd);
  }
  if (!TT.history)
    TT.history = dlist_add(&TT.history, "");

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

  // Let the mouldy old terminal mold us.
  TT.w = 80;
  TT.h = 24;
  terminal_size(&TT.w, &TT.h);

  // Let's rock!
  updateLine();
  handle_keys(0, handleEvent);

  // Clean up.
  tcsetattr(0, TCSANOW, &oldTermIo);
  puts("");
  fflush(stdout);
}
