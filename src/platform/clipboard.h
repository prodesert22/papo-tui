#ifndef PAPO_CLIPBOARD_H
#define PAPO_CLIPBOARD_H
#include "ui/input/text.h"
#include <stdint.h>
/* Explicit user copy only. Bounded native helper I/O is polled; terminals
 * without a local display receive OSC 52, encoded by GLib. */
bool clipboard_copy(const text_buffer *buffer);
/* Copies borrowed UTF-8 text, at most CLIPBOARD_TEXT_LIMIT bytes. */
#define CLIPBOARD_TEXT_LIMIT (256U * 1024U)
bool clipboard_copy_text(const char *utf8);
bool clipboard_poll(void);
const char *clipboard_notice(void);
uint64_t clipboard_revision(void);
void clipboard_shutdown(void);
#endif
