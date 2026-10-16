#include <stdint.h>
#ifndef PAPO_MENTION_POPUP_H
#define PAPO_MENTION_POPUP_H
#include "model/conversation/history.h"
#include "ui/input/text.h"
typedef struct mention_popup mention_popup;
bool mention_name_matches(const char *name, const char *query);
mention_popup *mention_popup_create(history *h);
void mention_popup_destroy(mention_popup *p);
bool mention_popup_update(mention_popup *p, text_buffer *b, bool enabled);
bool mention_popup_open(const mention_popup *p);
void mention_popup_close(mention_popup *p);
/* Consumes chooser controls only. accepted signals an atomic draft edit. */
bool mention_popup_key(mention_popup *p, text_buffer *b, int type, uint32_t key, bool *accepted);
bool mention_popup_mouse(mention_popup *p, text_buffer *b, const MEVENT *e, bool *accepted);
/* Returned window is borrowed until the next draw or close. */
WINDOW *mention_popup_draw(mention_popup *p, int cursor_y, int cursor_x, int left, int width);
#endif
