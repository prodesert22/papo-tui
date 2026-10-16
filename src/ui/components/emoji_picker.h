#include <stdint.h>
#ifndef PAPO_EMOJI_PICKER_H
#define PAPO_EMOJI_PICKER_H
#include "ui/media/icons.h"
typedef struct emoji_picker emoji_picker;
emoji_picker *emoji_picker_create(chat_assets *assets, icon_view *icons);
void emoji_picker_destroy(emoji_picker *p);
bool emoji_picker_open(const emoji_picker *p);
void emoji_picker_show(emoji_picker *p, const char *channel, uint64_t generation);
void emoji_picker_close(emoji_picker *p);
bool emoji_picker_update(emoji_picker *p, chat_assets *assets, const char *channel,
                         uint64_t generation);
bool emoji_picker_key(emoji_picker *p, text_buffer *editor, int type, uint32_t key, bool *inserted);
bool emoji_picker_mouse(emoji_picker *p, text_buffer *editor, const MEVENT *event, bool *inserted);
void emoji_picker_draw(emoji_picker *p);
bool emoji_picker_cursor(const emoji_picker *p);
#endif
