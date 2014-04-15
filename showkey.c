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

// Callback for incoming sequences from the terminal.
static int handleEvent(long extra, struct keyevent *event)
{
  int i;

  switch (event->type)
  {
    case HK_RAW :
    {
      printf("RAW ");
      for (i = 0; event->sequence[i]; i++)
      {
        printf("(%x) ", (int) event->sequence[i]);
        if (32 > event->sequence[i])
          printf("^%c, ", (int) event->sequence[i] + 'A' - 1);
        else
          printf("%c, ", (int) event->sequence[i]);
      }
      printf("-> ");
      break;
    }

    case HK_KEYS :
    {
      int l = strlen(event->sequence);

      if (event->isTranslated)
        printf("TRANSLATED - ");
      else
        printf("KEY - ");
      printf("%s\r\n", event->sequence);

      // Search for a key sequence bound to a command.
      for (i = 0; i < ARRAY_LEN(simpleKeys); i++)
      {
        if (strncmp(simpleKeys[i].key, event->sequence, l) == 0)
        {
          // If it's a partial match, keep accumulating them.
          if (strlen(simpleKeys[i].key) != l)
            return 0;
          else
            if (simpleKeys[i].handler)  simpleKeys[i].handler();
        }
      }
      break;
    }

    case HK_CSI :
    {
      // Is it a cursor location report?
      if (strcmp("R", event->sequence) == 0)
      {
        printf("CSI cursor position - line %d, column %d\r\n", event->params[0], event->params[1]);
        return 1;
      }

      printf("CSI command %s - ", event->sequence);
      for (i = 0; i < event->count; i++)
        printf("%d ", event->params[i]);
      printf("\r\n");
      break;
    }

    default :  break;
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

  handle_keys(0, handleEvent);

  tcsetattr(0, TCSANOW, &oldTermIo);
  puts("");
  fflush(stdout);
}
