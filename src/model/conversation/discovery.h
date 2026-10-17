#ifndef PAPO_MESSAGE_DISCOVERY_H
#define PAPO_MESSAGE_DISCOVERY_H
#include "model/conversation/history.h"
/* Event-loop APIs. Models own bounded JSON copies; accessors borrow until
 * poll/event/reset. No history reads and no notification read mutations. */
bool history_search(history *h, const json_t *filters);
void history_search_more(history *h);
json_t *history_search_results(history *h);
bool history_search_has_more(history *h);
const char *history_search_notice(history *h);
void history_discovery_close(history *h);
void history_pins_open(history *h);
void history_pins_refresh(history *h);
json_t *history_pins(history *h);
/* -1 unknown, 0 not pinned, 1 pinned. */
int history_pin_state(history *h, const char *message);
const char *history_pins_notice(history *h);
bool history_pin_message(history *h, const char *message, bool remove);
bool history_take_pin_result(history *h, char message[37], bool *success);
#endif
