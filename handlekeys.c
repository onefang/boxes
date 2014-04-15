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

// This table includes some variations I have found on some terminals.
// http://rtfm.etla.org/xterm/ctlseq.html has a useful guide.
// TODO - Don't think I got all the linux console variations.
// TODO - Add more shift variations, plus Ctrl & Alt variations when needed.
// TODO - tmux messes with the shift function keys somehow.
// TODO - Add other miscelany that does not use an escape sequence.

// This is sorted by type, though there is some overlap.
// Human typing speeds wont need fast searching speeds on this small table.
// So simple wins out over speed, and sorting by terminal type wins
// the simple test.
static struct key keys[] =
{
  // Control characters.
  //  Commented out coz it's the C string terminator, and may confuse things.
  //{"\x00",		"^@"},		// NUL
  {"\x01",		"^A"},		// SOH Apparently sometimes sent as Home.
  {"\x02",		"^B"},		// STX
  {"\x03",		"^C"},		// ETX SIGINT  Emacs and vi.
  {"\x04",		"^D"},		// EOT EOF     Emacs, joe, and nano.
  {"\x05",		"^E"},		// ENQ Apparently sometimes sent as End
  {"\x06",		"^F"},		// ACK
  {"\x07",		"^G"},		// BEL
  {"\x08",		"Del"},		// BS  Delete key, usually.
  {"\x09",		"Tab"},		// HT
  {"\x0A",		"Enter"},	// LF  Roxterm translates Ctrl-M to this.
  {"\x0B",		"^K"},		// VT
  {"\x0C",		"^L"},		// FF
  {"\x0D",		"Return"},	// CR  Other Enter/Return key, usually.
  {"\x0E",		"^N"},		// SO
  {"\x0F",		"^O"},		// SI  DISCARD
  {"\x10",		"^P"},		// DLE
  {"\x11",		"^Q"},		// DC1 SIGCONT  Vi.
  {"\x12",		"^R"},		// DC2
  {"\x13",		"^S"},		// DC3 SIGSTOP can't be caught.  Emacs and vi.
  {"\x14",		"^T"},		// DC4 SIGINFO STATUS
  {"\x15",		"^U"},		// NAK KILL character
  {"\x16",		"^V"},		// SYN LNEXT
  {"\x17",		"^W"},		// ETB WERASE
  {"\x18",		"^X"},		// CAN KILL character
  {"\x19",		"^Y"},		// EM  DSUSP SIGTSTP
  {"\x1A",		"^Z"},		// SUB SIGTSTP
  // Commented out coz it's the ANSI start byte in the below multibyte keys.
  // Handled in the code with a timeout.
  //{"\x1B",		"Esc"},		// ESC Esc key.
  {"\x1C",		"^\\"},		// FS SIGQUIT
  {"\x1D",		"^]"},		// GS
  {"\x1E",		"^^"},		// RS
  {"\x1F",		"^_"},		// US
  {"\x7F",		"BS"},		// Backspace key, usually.  Ctrl-? perhaps?
  // Commented out for the same reason Esc is.
  //{"\x9B",		"CSI"},		// CSI The eight bit encoding of "Esc [".

  // "Usual" xterm CSI sequences, with ";1" omitted for no modifiers.
  // Even though we have a proper CSI parser,
  // these should still be in this table.  Coz we would need a table anyway
  // in the CSI parser, so might as well keep them with the others.
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

  // "Normal" Some terminals are special, and it seems they only have
  //  four function keys.
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
  {"\x1BOQ",		"F2"},
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
};

static volatile sig_atomic_t sigWinch;
static int stillRunning;

static void handleSIGWINCH(int signalNumber)
{
    sigWinch = 1;
}

void handle_keys(long extra, int (*handle_event)(long extra, struct keyevent *event))
{
  struct keyevent event;
  fd_set selectFds;
  struct timespec timeOut;
  struct sigaction sigAction, oldSigAction;
  sigset_t signalMask;
  char buffer[20], sequence[20];
  int buffIndex = 0, pendingEsc = 0;

  buffer[0] = 0;
  sequence[0] = 0;

  // Terminals send the SIGWINCH signal when they resize.
  memset(&sigAction, 0, sizeof(sigAction));
  sigAction.sa_handler = handleSIGWINCH;
  sigAction.sa_flags = SA_RESTART;  // Useless if we are using poll.
  if (sigaction(SIGWINCH, &sigAction, &oldSigAction))
    perror_exit("can't set signal handler for SIGWINCH");
  sigemptyset(&signalMask);
  sigaddset(&signalMask, SIGWINCH);

  // TODO - OS buffered keys might be a problem, but we can't do the
  // usual timestamp filter for now.

  stillRunning = 1;
  while (stillRunning)
  {
    int j, p, csi = 0;

    // Apparently it's more portable to reset these each time.
    FD_ZERO(&selectFds);
    FD_SET(0, &selectFds);
    timeOut.tv_sec = 0;  timeOut.tv_nsec = 100000000; // One tenth of a second.

    // We got a "terminal size changed" signal, ask the terminal
    // how big it is now.
    if (sigWinch)
    {
      // Send - save cursor position, down 999, right 999,
      // request cursor position, restore cursor position.
      fputs("\x1B[s\x1B[999C\x1B[999B\x1B[6n\x1B[u", stdout);
      fflush(stdout);
      sigWinch = 0;
    }

    // TODO - Should only ask for a time out after we get an Escape, or
    //        the user requested time ticks.
    // I wanted to use poll, but that would mean using ppoll, which is
    // Linux only, and involves defining swear words to get it.
    p = pselect(0 + 1, &selectFds, NULL, NULL, &timeOut, &signalMask);
    if (0 > p)
    {
      if (EINTR == errno)
        continue;
      perror_exit("poll");
    }
    else if (0 == p)  // A timeout, trigger a time event.
    {
      if (pendingEsc)
      {
        // After a short delay to check, this is a real Escape key,
        // not part of an escape sequence, so deal with it.
        strcat(sequence, "Esc");
        buffer[0] = buffIndex = 0;
      }
      // TODO - Call some sort of timer tick callback.  This wont be
      //        a precise timed event, but don't think we need one.
    }
    else if ((0 < p) && FD_ISSET(0, &selectFds))
    {
      j = xread(0, &buffer[buffIndex], sizeof(buffer) - (buffIndex + 1));
      if (j == 0)    // End of file.
        stillRunning = 0;
      else
      {
        buffIndex += j;
        // Send raw keystrokes, mostly for things like showkey.
        event.type = HK_RAW;
        event.sequence = buffer;
        event.isTranslated = 0;
        handle_event(extra, &event);

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

    // Check for lone Esc first, wait a bit longer if it is
    pendingEsc = ((0 == buffer[1]) && ('\x1B' == buffer[0]));
    if (pendingEsc)  continue;

    // Check if it's a CSI before we check for the known key sequences.
    // C29B is the UTF8 encoding of CSI.
    // In all cases we reduce CSI to 9B to keep the keys table shorter.
    if ((('\x1B' == buffer[0]) && ('[' == buffer[1]))
      || (('\xC2' == buffer[0]) && ('\x9B' == buffer[1])))
    {
      buffer[0] = '\x9B';
      for (j = 1; buffer[j]; j++)
        buffer[j] = buffer[j + 1];
      buffIndex--;
    }
    csi = ('\x9B' == buffer[0]);

    // Check for known key sequences.
    // For a real timeout checked Esc, buffer is now empty, so this for loop
    // wont find it anyway.  While it's true we could avoid it by checking,
    // the user already had to wait for a time out, and this loop wont take THAT long.
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
       * So I'll include some notes here that tries to simplify that.
       *
       * The CSI format is - CSI [private] n1 ; n2 [extra] final
       * Each of those parts, except for the initial CSI bytes, is an ordinary
       * ASCII character.
       *
       * The optional [private] part is one of these characters "<=>?".
       * If the first byte is one of these, then this is a private command, if
       * it's one of the other n1 ones, it's not private.
       *
       * Next is a semi colon separated list of parameters (n1, n2, etc), which
       * can be any characters from this set "01234567890:;<=>?".  What the non
       * digit ones mean is up to the command.  Parameters can be left out, but
       * their defaults are command dependant.
       *
       * Next is an optional [extra] part from this set of characters
       * "!#$%&'()*+,-./", which includes double quotes.  Can be many of these,
       * likely isn't.
       *
       * Finally is the "final" from this set of characters "@[\]^_`{|}~", plus
       * upper and lower case letters.  It's private if it's one of these 
       * "pqrstuvwxyz{|}~".  Though the "private" ~ is used for key codes.
       *
       * A full CSI command is the private, extra, and final parts.
       *
       * Any C0 controls, DEL (0x7f), or higher characters are undefined.
       * TODO - So abort the current CSI and start from scratch on one of those.
       */

      if ('M' == buffer[1])
      {
        // We have a mouse report, which is CSI M ..., where the rest is
        // binary encoded, more or less.  Not fitting into the CSI format.
        // To make things worse, can't tell how long this will be.
        // So leave it up to the caller to tell us if they used it.
        event.type = HK_MOUSE;
        event.sequence = buffer;
        event.isTranslated = 0;
        if (handle_event(extra, &event))
        {
          buffer[0] = buffIndex = 0;
          sequence[0] = 0;
        }
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
            // Empty parameters are default parameters, so only deal with
            // non defaults.
            if (';' != buffer[csIndex] || (!t))
            {
              // TODO - Might be ":" in the number somewhere, but we are not
              // expecting any in anything we do.
              csParams[p] = atoi(&buffer[csIndex]);
            }
            p++;
            csIndex = j + 1;
          }
          j++;
        }
        while (t);

        // Check if we got the final byte, and send it to the callback.
        strcat(csFinal, &buffer[csIndex]);
        t = csFinal + strlen(csFinal) - 1;
        if (('\x40' <= (*t)) && ((*t) <= '\x7e'))
        {
          event.type = HK_CSI;
          event.sequence = csFinal;
          event.isTranslated = 1;
          event.count = p;
          event.params = csParams;
          handle_event(extra, &event);
          buffer[0] = buffIndex = 0;
          sequence[0] = 0;
        }
      }
    }

    // Pass the result to the callback.
    if (sequence[0] || buffer[0])
    {
      char b[strlen(sequence) + strlen(buffer) + 1];

      sprintf(b, "%s%s", sequence, buffer);
      event.type = HK_KEYS;
      event.sequence = b;
      event.isTranslated = (0 != sequence[0]);
      if (handle_event(extra, &event))
      {
        buffer[0] = buffIndex = 0;
        sequence[0] = 0;
      }
    }
  }

  sigaction(SIGWINCH, &oldSigAction, NULL);
}

void handle_keys_quit()
{
  stillRunning = 0;
}
