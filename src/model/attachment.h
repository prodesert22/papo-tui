#ifndef PAPO_ATTACHMENT_H
#define PAPO_ATTACHMENT_H
#include <gio/gio.h>
#include <stdbool.h>

/* Local resource limits, independent of the server's upload policy. */
#define ATTACHMENT_LIMIT 25
#define ATTACHMENT_STORAGE_LIMIT (512ULL * 1024 * 1024)
#define ATTACHMENT_CLIPBOARD_LIMIT (64U * 1024 * 1024)
typedef struct attachment_file attachment_file;
/* Blocking import: call from a worker. Immutable private copies are shared by
 * drafts and send snapshots; the last reference deletes the copy. */
attachment_file *attachment_import(const char *path, GCancellable *cancel, GError **error);
attachment_file *attachment_import_bytes(GBytes *bytes, const char *name, GCancellable *cancel,
                                         GError **error);
attachment_file *attachment_ref(attachment_file *file);
void attachment_unref(attachment_file *file);
const char *attachment_path(const attachment_file *file);
const char *attachment_name(const attachment_file *file);
const char *attachment_mime(const attachment_file *file);
guint64 attachment_id(const attachment_file *file);
guint64 attachment_size(const attachment_file *file);
#endif
