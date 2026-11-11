#define _POSIX_C_SOURCE 200809L
/* Recent search filters: newest first, at most five, no duplicates, without
 * the search text, and only when they name more than the order. */
#include "net/auth/client.h"
static bool fixture_session(const auth_client *client)
{
    return client != NULL;
}
#define auth_has_session fixture_session
#include "model/conversation/discovery.c"
#undef auth_has_session
#include <assert.h>

static json_t *by_author(int n, const char *text)
{
    char id[37];
    snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012d", n);
    return text ? json_pack("{s:s,s:s,s:s}", "author", id, "order", "desc", "text", text)
                : json_pack("{s:s,s:s}", "author", id, "order", "desc");
}

static bool search(history *h, json_t *filters)
{
    bool ok = filters && history_search(h, filters);
    json_decref(filters);
    return ok;
}

int main(void)
{
    auth_client *auth = auth_create();
    assert(auth);
    history *h = history_create(auth);
    assert(h);

    /* Text and order alone are not worth keeping. */
    assert(search(h, json_pack("{s:s,s:s}", "text", "olá", "order", "asc")));
    assert(history_search_recent_count(h) == 0);

    /* The text is dropped, so the same filters with other text are one entry. */
    assert(search(h, by_author(1, "primeira")));
    assert(search(h, by_author(1, "outra")));
    assert(history_search_recent_count(h) == 1);
    assert(!json_object_get(history_search_recent(h, 0), "text"));

    /* Newest first, at most five; repeating one moves it to the top. */
    for (int n = 2; n <= 7; ++n)
        assert(search(h, by_author(n, NULL)));
    assert(history_search_recent_count(h) == SEARCH_RECENT_LIMIT);
    json_t *seventh = by_author(7, NULL), *fourth = by_author(4, NULL);
    assert(json_equal(history_search_recent(h, 0), seventh));
    assert(search(h, json_incref(fourth)));
    assert(history_search_recent_count(h) == SEARCH_RECENT_LIMIT);
    assert(json_equal(history_search_recent(h, 0), fourth));
    assert(json_equal(history_search_recent(h, 1), seventh));
    json_decref(seventh);
    json_decref(fourth);

    /* Rejected filters are not kept. */
    assert(!search(h, json_pack("{s:s}", "author", "not-a-uuid")));
    assert(history_search_recent_count(h) == SEARCH_RECENT_LIMIT);

    history_destroy(h);
    auth_destroy(auth);
    return 0;
}
