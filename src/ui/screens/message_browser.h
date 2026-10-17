#ifndef PAPO_MESSAGE_BROWSER_H
#define PAPO_MESSAGE_BROWSER_H
#include "model/conversation/discovery.h"
#include "ui/input/text.h"
typedef struct message_browser message_browser;
message_browser *message_browser_create(history *h, auth_client *auth);
void message_browser_destroy(message_browser *m);
void message_browser_open(message_browser *m, bool pins);
void message_browser_close(message_browser *m);
bool message_browser_is_open(const message_browser *m);
void message_browser_key(message_browser *m, int type, uint32_t key);
WINDOW *message_browser_draw(message_browser *m);
bool message_browser_cursor(message_browser *m);
const char *message_browser_destination(const message_browser *m);
const char *message_browser_destination_message(const message_browser *m);
const char *message_browser_destination_time(const message_browser *m);
void message_browser_destination_clear(message_browser *m);
#endif
