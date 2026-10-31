#ifndef PAPO_VIEW_STATE_H
#define PAPO_VIEW_STATE_H
#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>

/* The conversation last shown on each server, and which server was used last.
 * Public identifiers only: normalized instance URLs and conversation UUIDs.
 * Stored privately beside the read cursors and atomically replaced; failures
 * leave the previous state. */
bool view_state_validate(json_t *root);
/* Records conversation (a UUID, or "" when none is open) as the last one of
 * url, and url as the most recently used server. */
bool view_state_save(const char *url, const char *conversation);
/* Copies the last conversation of url, or "" when unknown. */
void view_state_conversation(const char *url, char conversation[37]);
/* Index in urls of the most recently used server, or SIZE_MAX when none of
 * them was recorded. */
size_t view_state_latest(const char *const *urls, size_t count);
#endif
