/* handlekeys.h - Generic terminal input handler.
 *
 * Copyright 2012 David Seikel <won_fang@yahoo.com.au>
 */

void handle_keys(long extra, int (*handle_sequence)(long extra, char *sequence), void (*handle_CSI)(long extra, char *command, int *params, int count));
void handle_keys_quit();
