/* handlekeys.h - Generic terminal input handler.
 *
 * Copyright 2012 David Seikel <won_fang@yahoo.com.au>
 */

/* An input loop that handles keystrokes and terminal CSI commands.
 *
 * Reads stdin, trying to convert raw keystrokes into something more readable.
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
 * a SIGWINCH.  This is the main reason for handle_CSI, as those reports are
 * sent as CSI.  It's still up to the user code to recognise and deal with the
 * terminal resize response, but at least it's nicely decoded for you.
 *
 * Arguments -
 *  extra           - arbitrary data that gets passed back to the callbacks.
 *  handle_sequence - a callback to handle keystroke sequences.
 *  handle_CSI      - a callback to handle terminal CSI commands.
 *
 * handle_sequence is called when a complete keystroke sequence has been
 * accumulated.  It should return 1 if the sequence has been dealt with,
 * otherwise it should return 0, then handle_keys will keep adding more
 * complete keystroke sequences on the end, and try again later.
 *
 * handle_CSI is called when a complete terminal CSI command has been
 * detected.  The command argument is the full CSI command code, including
 * private and intermediate characters.  The params argument is the decoded
 * parameters from the command.  The count argument is the number of decoded
 * parameters.  Empty parameters are set to -1, coz -1 parameters are not legal,
 * and empty ones should default to something that is command dependant.
 *
 * NOTE - handle_CSI differs from handle_sequence in not having to
 * return anything.  CSI sequences include a definite terminating byte,
 * so no need for this callback to tell handle_keys to keep accumulating.
 * Some applications use a series of keystrokes for things, so they
 * get accumulated until fully recognised by the user code.
 */
void handle_keys(long extra, 
  int (*handle_sequence)(long extra, char *sequence),
  void (*handle_CSI)(long extra, char *command, int *params, int count));


/* Call this when you want handle_keys to return. */
void handle_keys_quit();
