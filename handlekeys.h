/* handlekeys.h - Generic terminal input handler.
 *
 * Copyright 2012 David Seikel <won_fang@yahoo.com.au>
 */

enum keyeventtype{
  HK_CSI,
  HK_KEYS,
  HK_MOUSE,
  HK_RAW
};

struct keyevent {
  enum keyeventtype type;	// The type of this event.
  char *sequence;		// Either a translated sequence, or raw bytes.
  int isTranslated;		// Whether or not sequence is translated.
  int count;			// Number of entries in params.
  int *params;			// For CSI events, the decoded parameters.
};

/* An input loop that handles keystrokes and terminal CSI commands.
 *
 * Reads stdin, trying to translate raw keystrokes into something more readable.
 * See the keys[] array at the top of handlekeys.c for what byte sequences get
 * translated into what key names.  See dumbsh.c for an example of usage.
 * A 0.1 second delay is used to detect the Esc key being pressed, and not Esc
 * being part of a raw keystroke.
 *
 * handle_keys also tries to decode CSI commands that terminals can send.
 * Some keystrokes are CSI commands, but those are translated as key sequences
 * instead of CSI commands.
 *
 * handle_keys also sets up a SIGWINCH handler to catch terminal resizes,
 * and sends a request to the terminal to report it's current size when it gets
 * a SIGWINCH.  This is the main reason for HK_CSI, as those reports are
 * sent as CSI.  It's still up to the user code to recognise and deal with the
 * terminal resize response, but at least it's nicely decoded for you.
 *
 * Arguments -
 *  extra           - arbitrary data that gets passed back to the callbacks.
 *  handle_event    - a callback to handle sequences.
 *
 * handle_event is called when a complete sequence has been accumulated.  It is
 * passed a keyevent structure.  The type member of that structure determines
 * what sort of event this is.  What's in the rest of the keyevent depends on
 * the type -
 *
 * HK_CSI
 *   sequence is the fully decoded CSI command, including the private  and intermediate characters.
 *   isTranslated is 1, since the CSI command has been translated.
 *   count is the count of translated CSI parameters.
 *   params is an array of translateted CSI parameters.
 *   Empty parameters are set to -1, coz -1 parameters are not legal,
 *   and empty ones should default to something that is command dependant.
 *
 * HK_KEYS
 *  sequence the keystrokes as ASCII, either translated or not.
 *  isTranslated if 0, then sequence is ordinary keys, otherwise
 *  sequence is the names of keys, from the keys[] array.
 *  count and params are not used.
 *
 * For HK_KEYS handle_event should return 1 if the sequence has been dealt with,
 * or ignored.  It should return 0, if handle_keys should keep adding more
 * translated keystroke sequences on the end, and try again later.
 * 0 should really only be used if it's a partial match, and we need more
 * keys in the sequence to make a full match.
 *
 * HK_MOUSE
 *   sequence is the raw bytes of the mouse report.  The rest are not used.
 *
 */
void handle_keys(long extra, int (*handle_event)(long extra, struct keyevent *event));


/* Call this when you want handle_keys to return. */
void handle_keys_quit();
