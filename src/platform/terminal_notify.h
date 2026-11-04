#ifndef PAPO_TERMINAL_NOTIFY_H
#define PAPO_TERMINAL_NOTIFY_H
#include <stdbool.h>
/* Notifications shown by the terminal itself (Kitty's OSC 99 protocol). When
 * one is clicked the terminal focuses its own window and the tab it came from,
 * which a separate desktop notification cannot do, and reports the click on
 * stdin. Main thread only. */

/* Asks once whether the terminal supports the protocol. */
void terminal_notify_probe(void);
/* Takes a complete OSC 99 reply read from the terminal. Returns false when
 * reply is not OSC 99; every OSC 99 reply is consumed, even an unknown one. */
bool terminal_notify_reply(const char *reply);
/* True once the terminal said it focuses and reports clicked notifications. */
bool terminal_notify_ready(void);
/* id must be nonzero. Title and body are UTF-8; control characters become
 * spaces and each field is cut to the protocol's 2048 bytes. */
bool terminal_notify_send(unsigned id, const char *title, const char *body);
void terminal_notify_close(unsigned id);
/* Pops the id of one clicked notification. */
bool terminal_notify_take_activation(unsigned *id);
#endif
