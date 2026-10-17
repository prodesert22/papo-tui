#ifndef PAPO_FILE_TRANSFER_H
#define PAPO_FILE_TRANSFER_H
#include "model/attachment.h"
typedef struct {
    GPtrArray *paths; /* Owned filename strings. */
    GBytes *image;
    char *image_name, *text;
} clipboard_content;
void clipboard_content_free(clipboard_content *content);
/* Main-thread task registry. Shutdown drains cancelled workers after curses. */
GTask *file_transfer_task_new(GCancellable *cancel);
void file_transfer_tasks_poll(void);
void file_transfer_shutdown(void);
/* Blocking platform operations: worker only. Cancellation is distinct from
 * unavailable support so closing a native dialog never opens another dialog. */
GPtrArray *platform_pick_files(GCancellable *cancel, GError **error);
clipboard_content *platform_read_clipboard(GCancellable *cancel, GError **error);
#endif
