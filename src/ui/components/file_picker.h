#ifndef PAPO_FILE_PICKER_H
#define PAPO_FILE_PICKER_H
#include "ui/input/text.h"
#include <gio/gio.h>
typedef struct file_picker file_picker;
file_picker *file_picker_create(void);
void file_picker_destroy(file_picker *picker);
void file_picker_show(file_picker *picker);
void file_picker_close(file_picker *picker);
bool file_picker_open(const file_picker *picker);
bool file_picker_poll(file_picker *picker);
bool file_picker_key(file_picker *picker, int type, uint32_t key);
bool file_picker_mouse(file_picker *picker, const MEVENT *event);
bool file_picker_paste(file_picker *picker, text_paste *paste);
/* Borrowed overlay window; register its bounds with existing media clipping. */
WINDOW *file_picker_draw(file_picker *picker);
GPtrArray *file_picker_take_files(file_picker *picker);
#endif
