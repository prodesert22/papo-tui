#ifndef PAPO_MEDIA_PROCESS_H
#define PAPO_MEDIA_PROCESS_H
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
/* Takes a floating payload; returns an owned (tbv) response or NULL. Safe from
 * media workers. The caller may cancel and release its context immediately. */
GVariant *media_process_call(unsigned operation, GVariant *payload, bool (*cancelled)(void *),
                             void *context);
uint64_t media_process_generation(void);
void media_process_stop(void);
size_t media_process_released_bytes(void);
#endif
