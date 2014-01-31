/* handlekeys.c - Generic terminal input handler.
 *
 * Copyright 2012 David Seikel <won_fang@yahoo.com.au>
 */

// I use camelCaseNames internally, instead of underscore_names as is preferred
// in the rest of toybox.  A small limit of 80 characters per source line infers
// shorter names should be used.  CamelCaseNames are shorter.  Externally visible
// stuff is underscore_names as usual.  Plus, I'm used to camelCaseNames, my
// fingers twitch that way.

#include "toys.h"
#include "handlekeys.h"

struct key
{
  char *code;
  char *name;
};

// This table includes some variations I have found on some terminals, and the MC "Esc digit" versions.
// http://rtfm.etla.org/xterm/ctlseq.html has a useful guide.
// TODO - Don't think I got all the linux console variations.
// TODO - Add more shift variations, plus Ctrl & Alt variations when needed.
// TODO - tmux messes with the shift function keys somehow.
// TODO - Add other miscelany that does not use an escape sequence.

// This is sorted by type, though there is some overlap.
// Human typing speeds wont need binary searching speeds on this small table.
// So simple wins out over speed, and sorting by terminal type wins the simple test.
static struct key keys[] =
{
  // Control characters.
//  {"\x00",		"^@"},		// NUL Commented out coz it's the C string terminator, and may confuse things.
  {"\x01",		"^A"},		// SOH Apparently sometimes sent as Home
  {"\x02",		"^B"},		// STX
  {"\x03",		"^C"},		// ETX SIGINT  Emacs and vi.
  {"\x04",		"^D"},		// EOT EOF     Emacs, joe, and nano.
  {"\x05",		"^E"},		// ENQ Apparently sometimes sent as End
  {"\x06",		"^F"},		// ACK
  {"\x07",		"^G"},		// BEL
  {"\x08",		"Del"},		// BS  Delete key, usually.
  {"\x09",		"Tab"},		// HT  Tab key.
  {"\x0A",		"Return"},	// LF  Return key.  Roxterm at least is translating both Ctrl-J and Ctrl-M into this.
  {"\x0B",		"^K"},		// VT
  {"\x0C",		"^L"},		// FF
  {"\x0D",		"^M"},		// CR  Other Return key, usually.
  {"\x0E",		"^N"},		// SO
  {"\x0F",		"^O"},		// SI  DISCARD
  {"\x10",		"^P"},		// DLE
  {"\x11",		"^Q"},		// DC1 SIGCONT  Vi, and made up commands in MC, which seem to work anyway.
  {"\x12",		"^R"},		// DC2
  {"\x13",		"^S"},		// DC3 SIGSTOP can't be caught.  Emacs and vi, so much for "can't be caught".
  {"\x14",		"^T"},		// DC4 SIGINFO STATUS
  {"\x15",		"^U"},		// NAK KILL character
  {"\x16",		"^V"},		// SYN LNEXT
  {"\x17",		"^W"},		// ETB WERASE
  {"\x18",		"^X"},		// CAN KILL character
  {"\x19",		"^Y"},		// EM  DSUSP SIGTSTP
  {"\x1A",		"^Z"},		// SUB SIGTSTP
//  {"\x1B",		"^["},		// ESC Esc key.  Commented out coz it's the ANSI start byte in the below multibyte keys.  Handled in the code with a timeout.
  {"\x1C",		"^\\"},		// FS SIGQUIT  Some say ^D is SIGQUIT, but my tests say it's this.
  {"\x1D",		"^]"},		// GS
  {"\x1E",		"^^"},		// RS
  {"\x1F",		"^_"},		// US
  {"\x7F",		"BS"},		// Backspace key, usually.  Ctrl-? perhaps?
//  {"\x9B",		"CSI"},		// CSI The eight bit encoding of "Esc [".  Commented out for the same reason Esc is.

  // "Usual" xterm CSI sequences, with ";1" omitted for no modifiers.
  // Even though we have a proper CSI parser, these should still be in this table.
  // Coz we would need a table anyway in the CSI parser, so might as well keep them with the others.
  // Also, less code, no need to have a separate scanner for that other table.
  {"\x9B\x31~",		"Home"},	// Duplicate, think I've seen this somewhere.
  {"\x9B\x32~",		"Ins"},
  {"\x9B\x33~",		"Del"},
  {"\x9B\x34~",		"End"},		// Duplicate, think I've seen this somewhere.
  {"\x9B\x35~",		"PgUp"},
  {"\x9B\x36~",		"PgDn"},
  {"\x9B\x37~",		"Home"},
  {"\x9B\x38~",		"End"},
  {"\x9B\x31\x31~",		"F1"},
  {"\x9B\x31\x32~",		"F2"},
  {"\x9B\x31\x33~",		"F3"},
  {"\x9B\x31\x34~",		"F4"},
  {"\x9B\x31\x35~",		"F5"},
  {"\x9B\x31\x37~",		"F6"},
  {"\x9B\x31\x38~",		"F7"},
  {"\x9B\x31\x39~",		"F8"},
  {"\x9B\x32\x30~",		"F9"},
  {"\x9B\x32\x31~",		"F10"},
  {"\x9B\x32\x33~",		"F11"},
  {"\x9B\x32\x34~",		"F12"},

  // As above, ";2" means shift modifier.
  {"\x9B\x31;2~",		"Shift Home"},
  {"\x9B\x32;2~",		"Shift Ins"},
  {"\x9B\x33;2~",		"Shift Del"},
  {"\x9B\x34;2~",		"Shift End"},
  {"\x9B\x35;2~",		"Shift PgUp"},
  {"\x9B\x36;2~",		"Shift PgDn"},
  {"\x9B\x37;2~",		"Shift Home"},
  {"\x9B\x38;2~",		"Shift End"},
  {"\x9B\x31\x31;2~",	"Shift F1"},
  {"\x9B\x31\x32;2~",	"Shift F2"},
  {"\x9B\x31\x33;2~",	"Shift F3"},
  {"\x9B\x31\x34;2~",	"Shift F4"},
  {"\x9B\x31\x35;2~",	"Shift F5"},
  {"\x9B\x31\x37;2~",	"Shift F6"},
  {"\x9B\x31\x38;2~",	"Shift F7"},
  {"\x9B\x31\x39;2~",	"Shift F8"},
  {"\x9B\x32\x30;2~",	"Shift F9"},
  {"\x9B\x32\x31;2~",	"Shift F10"},
  {"\x9B\x32\x33;2~",	"Shift F11"},
  {"\x9B\x32\x34;2~",	"Shift F12"},

  // "Normal" Some terminals are special, and it seems they only have four function keys.
  {"\x9B\x41",		"Up"},
  {"\x9B\x42",		"Down"},
  {"\x9B\x43",		"Right"},
  {"\x9B\x44",		"Left"},
  {"\x9B\x46",		"End"},
  {"\x9BH",		"Home"},
  {"\x9BP",		"F1"},
  {"\x9BQ",		"F2"},
  {"\x9BR",		"F3"},
  {"\x9BS",		"F4"},
  {"\x9B\x31;2P",		"Shift F1"},
  {"\x9B\x31;2Q",		"Shift F2"},
  {"\x9B\x31;2R",		"Shift F3"},
  {"\x9B\x31;2S",		"Shift F4"},

  // "Application"  Esc O is known as SS3
  {"\x1BOA",		"Up"},
  {"\x1BOB",		"Down"},
  {"\x1BOC",		"Right"},
  {"\x1BOD",		"Left"},
  {"\x1BOF",		"End"},
  {"\x1BOH",		"Home"},
  {"\x1BOn",		"Del"},
  {"\x1BOp",		"Ins"},
  {"\x1BOq",		"End"},
  {"\x1BOw",		"Home"},
  {"\x1BOP",		"F1"},
  {"\x1BOO",		"F2"},
  {"\x1BOR",		"F3"},
  {"\x1BOS",		"F4"},
  {"\x1BOT",		"F5"},
  // These two conflict with the above four function key variations.
  {"\x9BR",		"F6"},
  {"\x9BS",		"F7"},
  {"\x9BT",		"F8"},
  {"\x9BU",		"F9"},
  {"\x9BV",		"F10"},
  {"\x9BW",		"F11"},
  {"\x9BX",		"F12"},

  // Can't remember, but saw them somewhere.
  {"\x1BO1;2P",		"Shift F1"},
  {"\x1BO1;2Q",		"Shift F2"},
  {"\x1BO1;2R",		"Shift F3"},
  {"\x1BO1;2S",		"Shift F4"},

  // MC "Esc digit" specials.
  // NOTE - The MC Esc variations might not be such a good idea, other programs want the Esc key for other things.
  //          Notably seems that "Esc somekey" is used in place of "Alt somekey" AKA "Meta somekey" coz apparently some OSes swallow those.
  //            Conversely, some terminals send "Esc somekey" when you do "Alt somekey".
  //          MC Esc variants might be used on Macs for other things?
  {"\x1B\x31",		"F1"},
  {"\x1B\x32",		"F2"},
  {"\x1B\x33",		"F3"},
  {"\x1B\x34",		"F4"},
  {"\x1B\x35",		"F5"},
  {"\x1B\x36",		"F6"},
  {"\x1B\x37",		"F7"},
  {"\x1B\x38",		"F8"},
  {"\x1B\x39",		"F9"},
  {"\x1B\x30",		"F10"}
};

static volatile sig_atomic_t sigWinch;
static int stillRunning;

static void handleSIGWINCH(int signalNumber)
{
    sigWinch = 1;
}

// TODO - Unhandled complications -
//   Less and more have the "ZZ" command, but nothing else seems to have multi ordinary character commands.
void handle_keys(long extra, int (*handle_sequence)(long extra, char *sequence), void (*handle_CSI)(long extra, char *command, int *params, int count))
{
  fd_set selectFds;
  struct timespec timeOut;
  struct sigaction sigAction, oldSigAction;
  sigset_t signalMask;
  char buffer[20], sequence[20];
  int buffIndex = 0;

  buffer[0] = 0;
  sequence[0] = 0;

  // Terminals send the SIGWINCH signal when they resize.
  memset(&sigAction, 0, sizeof(sigAction));
  sigAction.sa_flags = SA_RESTART;// Useless if we are using poll.
  if (sigaction(SIGWINCH, &sigAction, &oldSigAction))  perror_exit("can't set signal handler SIGWINCH");
  sigAction.sa_handler = handleSIGWINCH;
  sigemptyset(&signalMask);
  sigaddset(&signalMask, SIGWINCH);

  // TODO - OS buffered keys might be a problem, but we can't do the usual timestamp filter for now.
  stillRunning = 1;
  while (stillRunning)
  {
    int j, p, csi = 0;

    // Apparently it's more portable to reset these each time.
    FD_ZERO(&selectFds);
    FD_SET(0, &selectFds);
    timeOut.tv_sec = 0;  timeOut.tv_nsec = 100000000; // One tenth of a second.

// TODO - A bit unstable at the moment, something makes it go into a horrid CPU eating edit line flicker mode sometimes.  And / or vi mode can crash on exit (stack smash).
//          This might be fixed now.

    // We got a "terminal size changed" signal, ask the terminal how big it is now.
    if (sigWinch)
    {
      // Send - save cursor position, down 999, right 999, request cursor position, restore cursor position.
      fputs("\x1B[s\x1B[999C\x1B[999B\x1B[6n\x1B[u", stdout);
      fflush(stdout);
      sigWinch = 0;
    }

    // TODO - Should only ask for a time out after we get an Escape, or the user requested time ticks.
    // I wanted to use poll, but that would mean using ppoll, which is Linux only, and involves defining swear words to get it.
    p = pselect(0 + 1, &selectFds, NULL, NULL, &timeOut, &signalMask);
    if (0 > p)
    {
      if (EINTR == errno)
        continue;
      perror_exit("poll");
    }
    else if (0 == p)          // A timeout, trigger a time event.
    {
      if ((0 == buffer[1]) && ('\x1B' == buffer[0]))
      {
        // After a short delay to check, this is a real Escape key, not part of an escape sequence, so deal with it.
        // TODO - so far the only uses of this have the escape at the start, but maybe a strcat is needed instead later?
        strcpy(sequence, "^[");
        buffer[0] = buffIndex = 0;
      }
      // TODO - Call some sort of timer tick callback.  This wont be a precise timed event, but don't think we need one.
    }
    else if ((0 < p) && FD_ISSET(0, &selectFds))
    {
      // I am assuming that we get the input atomically, each multibyte key fits neatly into one read.
      // If that's not true (which is entirely likely), then we have to get complicated with circular buffers and stuff, or just one byte at a time.
      j = read(0, &buffer[buffIndex], sizeof(buffer) - (buffIndex + 1));
      if (j < 0)      // An error happened.
      {
        // For now, just ignore errors.
        fprintf(stderr, "input error on %d\n", p);
        fflush(stderr);
      }
      else if (j == 0)    // End of file.
      {
        stillRunning = 0;
        fprintf(stderr, "EOF\n");
        for (j = 0; buffer[j + 1]; j++)
          fprintf(stderr, "(%x), ", (int) buffer[j]);
        fflush(stderr);
      }
      else
      {
        buffIndex += j;
        if (sizeof(buffer) < (buffIndex + 1))  // Ran out of buffer.
        {
          fprintf(stderr, "Full buffer - %s  ->  %s\n", buffer, sequence);
          for (j = 0; buffer[j + 1]; j++)
            fprintf(stderr, "(%x) %c, ", (int) buffer[j], buffer[j]);
          fflush(stderr);
          buffIndex = 0;
        }
        buffer[buffIndex] = 0;
      }
    }

    // Check if it's a CSI before we check for the known key sequences.
    if ('\x9B' == buffer[0])
      csi = 1;
    if (('\x1B' == buffer[0]) && ('[' == buffer[1]))
      csi = 2;
    if (('\xC2' == buffer[0]) && ('\x9B' == buffer[1]))
      csi = 2;
    if (2 == csi)
    {
      buffer[0] = '\x9B';
      for (j = 1; buffer[j]; j++)
        buffer[j] = buffer[j + 1];
      buffIndex--;
      csi = 1;
    }

    // Check for known key sequences.
    // For a real timeout checked Esc, buffer is now empty, so this for loop wont find it anyway.
    // While it's true we could avoid it by checking, the user already had to wait for a time out, and this loop wont take THAT long.
    for (j = 0; keys[j].code; j++)    // Search for multibyte keys and control keys.
    for (j = 0; j < (sizeof(keys) / sizeof(*keys)); j++)
    {
      if (strcmp(keys[j].code, buffer) == 0)
      {
        strcat(sequence, keys[j].name);
        buffer[0] = buffIndex = 0;
        csi = 0;
        break;
      }
    }

    // Find out if it's a CSI sequence that's not in the known key sequences.
    if (csi)
    {
      /* ECMA-048 section 5.2 defines this, and is unreadable.
       * General CSI format - CSI [private] n1 ; n2 [extra] final
       *   private  0x3c to 0x3f  "<=>?" If first byte is one of these,
       *                                 this is a private command, if it's
       *                                 one of the other n1 ones,
       *                                 it's not private.
       *   n1       0x30 to 0x3f  "01234567890:;<=>?"
       *                                 ASCII digits forming a "number"
       *            0x3a          ":"    Used for floats, not expecting any.
       *                                 Could also be used as some other sort of
       *                                 inter digit separator.
       *            0x3b [;]             Separates the parameters.
       *   extra    0x20 to 0x2f  [ !"#$%&'()*+,-./]
       *                                 Can be multiple, likely isn't.
       *   final    0x40 to 0x7e  "@A .. Z[\]^_`a .. z{|}~"
       *                                  It's private if 0x70 to 0x7e "p .. z{|}~"
       *                                  Though the "private" ~ is used for key codes.
       *                                  We also have SS3 "\x1BO" for other keys,
       *                                  but that's not a CSI.
       * C0 controls, DEL (0x7f), or high characters are undefined.
       * TODO - So abort the current CSI and start from scratch on one of those.
       */

      if ('M' == buffer[1])
      {
        // TODO - We have a mouse report, which is CSI M ..., where the rest is binary encoded, more or less.  Not fitting into the CSI format.
      }
      else
      {
        char *t, csFinal[8];
        int csIndex = 1, csParams[8];

        csFinal[0] = 0;
        p = 0;

        // Unspecified params default to a value that is command dependant.
        // However, they will never be negative, so we can use -1 to flag
        // a default value.
        for (j = 0; j < (sizeof(csParams) / sizeof(*csParams)); j++)
          csParams[j] = -1;

        // Check for the private bit.
        if (index("<=>?", buffer[1]))
        {
          csFinal[0] = buffer[1];
          csFinal[1] = 0;
          csIndex++;
        }

        // Decode parameters.
        j = csIndex;
        do
        {
          // So we know when we get to the end of parameter space.
          t = index("01234567890:;<=>?", buffer[j + 1]);
          // See if we passed a paremeter.
          if ((';' == buffer[j]) || (!t))
          {
            // Only stomp on the ; if it's really the ;.
            if (t)
              buffer[j] = 0;
            // Empty parameters are default parameters, so only deal with non defaults.
            if (';' != buffer[csIndex] || (!t))
            {
              // TODO - Might be ":" in the number somewhere, but we are not expecting any in anything we do.
              csParams[p] = atoi(&buffer[csIndex]);
            }
            p++;
            csIndex = j + 1;
          }
          j++;
        }
        while (t);

        // Get the final command sequence, and pass it to the callback.
        strcat(csFinal, &buffer[csIndex]);
        if (handle_CSI)
          handle_CSI(extra, csFinal, csParams, p);
      }

      csi = 0;
      // Wether or not it's a CSI we understand, it's been handled either here or in the key sequence scanning above.
      buffer[0] = buffIndex = 0;
    }

    // Pass the result to the callback.
    if ((handle_sequence) && (sequence[0] || buffer[0]))
    {
      char b[strlen(sequence) + strlen(buffer) + 1];

      sprintf(b, "%s%s", sequence, buffer);
      if (handle_sequence(extra, b))
      {
        sequence[0] = 0;
        buffer[0] = buffIndex = 0;
      }
    }
  }

  sigaction(SIGWINCH, &oldSigAction, NULL);
}

void handle_keys_quit()
{
  stillRunning = 0;
}
