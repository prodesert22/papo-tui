#ifndef PAPO_GIF_SEARCH_H
#define PAPO_GIF_SEARCH_H
#include "net/http/download.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GIF search on Wikimedia Commons, whose public API needs no key. Queries go
 * through the shared download transport: https only, no cookies, no redirects,
 * the global request budget, and one transfer at a time on the event loop.
 * Everything in a response is untrusted: titles are sanitized and only
 * Wikimedia addresses (or, for tests, the configured origin) are accepted. */
typedef struct {
    char *title;     /* Printable UTF-8, without the "File:" prefix or extension. */
    char *thumb_url; /* Animated thumbnail shown in the picker. */
    char *url;       /* Original GIF, the address inserted into the message. */
    int width, height;
} gif_result;
typedef enum { GIF_SEARCH_IDLE, GIF_SEARCH_LOADING, GIF_SEARCH_READY, GIF_SEARCH_FAILED } gif_state;
typedef struct gif_search gif_search;

/* PAPO_GIF_SEARCH_URL replaces the endpoint, for tests against a local mock;
 * results must then also come from that URL's origin. */
gif_search *gif_search_create(auth_client *auth);
void gif_search_destroy(gif_search *s);
/* Replaces any pending or running query; it starts after a short pause so
 * typing does not send one request per key. */
void gif_search_request(gif_search *s, const char *query, uint64_t now);
/* Runs on the main thread. Returns true when the state or results changed. */
bool gif_search_poll(gif_search *s, uint64_t now);
gif_state gif_search_state(const gif_search *s);
size_t gif_search_count(const gif_search *s);
const gif_result *gif_search_result(const gif_search *s, size_t index);
const char *gif_search_error(const gif_search *s);
/* Exposed for tests: parses a Commons response; `origin` NULL means Wikimedia. */
bool gif_search_parse(const char *text, size_t length, const char *origin, gif_result **results,
                      size_t *count);
void gif_results_free(gif_result *results, size_t count);
#endif
