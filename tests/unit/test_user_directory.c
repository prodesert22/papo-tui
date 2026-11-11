/* The user directory keeps its loaded entries through a revalidation, never
 * duplicates an entry and keeps the indices that open panels borrow. */
#include "model/conversation/private.h"
#include "model/text/translation.h"
#include "net/clock.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void user_id(char id[37], int n)
{
    snprintf(id, 37, "00000000-0000-4000-8000-%012d", n);
}

/* Users first..first+count-1 that are present in this pass, as one page. */
static json_t *page(int first, int count, const int *missing, size_t missing_count, bool more)
{
    json_t *users = json_array();
    for (int n = first; users && n < first + count; ++n) {
        bool skip = false;
        for (size_t i = 0; i < missing_count; ++i)
            skip |= missing[i] == n;
        if (skip)
            continue;
        char id[37], name[32], created[40];
        user_id(id, n);
        snprintf(name, sizeof(name), "user%d", n);
        snprintf(created, sizeof(created), "2026-10-15T12:00:00.%06dZ", n);
        json_array_append_new(users, json_pack("{s:s,s:s,s:s,s:[]}", "id", id, "username", name,
                                               "created_at", created, "roles"));
    }
    return json_pack("{s:o,s:b}", "users", users, "has_more", more);
}

static bool feed(history *h, json_t *body)
{
    bool ok = body && history_internal_users_response(h, body);
    json_decref(body);
    return ok;
}

static bool listed(history *h, int n)
{
    char id[37];
    user_id(id, n);
    for (size_t i = 0; i < history_user_count(h); ++i) {
        json_t *u = history_user(h, i);
        if (u && !strcmp(history_internal_str(u, "id"), id))
            return true;
    }
    return false;
}

int main(void)
{
    auth_client *auth = auth_create();
    CHECK(auth);
    history *h = history_create(auth);
    CHECK(h);

    /* Two pages, then a repeated page: no entry is listed twice. */
    CHECK(feed(h, page(0, 100, NULL, 0, true)));
    CHECK(feed(h, page(100, 50, NULL, 0, false)));
    CHECK(history_user_count(h) == 150 && history_users_complete(h));
    CHECK(feed(h, page(0, 100, NULL, 0, true)));
    CHECK(history_user_count(h) == 150 && json_object_size(h->listed) == 150);

    /* Opening again right away reuses the complete directory. */
    char cursor[37];
    snprintf(cursor, sizeof(cursor), "%s", h->users_id);
    history_users_open(h);
    CHECK(h->users_active && history_users_complete(h) && !strcmp(h->users_id, cursor));

    /* A revalidation keeps the entries listed and usable while it runs. */
    history_users_refresh(h);
    CHECK(!history_users_complete(h) && !*h->users_id && history_user_count(h) == 150);
    CHECK(strcmp(history_users_notice(h), _("Carregando usuários…")));
    CHECK(feed(h, page(0, 100, NULL, 0, true)));
    CHECK(history_user_count(h) == 150 && listed(h, 145));

    /* Users the complete pass no longer returns leave the list without moving
     * the other entries; their names still resolve for the history. */
    const int gone[] = {140, 141, 142, 143, 144, 145, 146, 147, 148, 149};
    char first[37];
    snprintf(first, sizeof(first), "%s", history_internal_str(history_user(h, 0), "id"));
    CHECK(feed(h, page(100, 50, gone, 10, false)));
    CHECK(history_users_complete(h) && history_user_count(h) == 150);
    CHECK(!history_user(h, 145) && history_user(h, 139) && !listed(h, 145));
    CHECK(!strcmp(history_internal_str(history_user(h, 0), "id"), first));
    char id[37];
    user_id(id, 145);
    CHECK(history_mention_name(h, id) && !strcmp(history_mention_name(h, id), "user145"));

    /* Maintenance compacts only once no panel borrows indices. */
    uint64_t now = 0;
    CHECK(net_now_ms(&now));
    CHECK(h->users_active);
    CHECK(!history_maintain(h, now + 2000) && history_user_count(h) == 150);
    history_users_active(h, false);
    CHECK(history_maintain(h, now + 4000));
    CHECK(history_user_count(h) == 140 && json_object_size(h->listed) == 140);
    for (size_t i = 0; i < history_user_count(h); ++i)
        CHECK(history_user(h, i));
    CHECK(history_users_complete(h)); /* Nothing was evicted: still complete. */

    /* A user dropped by one pass and returned by the next is listed again. */
    history_users_refresh(h);
    const int first_gone[] = {0};
    CHECK(feed(h, page(0, 100, first_gone, 1, true)));
    CHECK(feed(h, page(100, 40, NULL, 0, false)));
    CHECK(!listed(h, 0) && history_user_count(h) == 140);
    history_users_refresh(h);
    CHECK(feed(h, page(0, 100, NULL, 0, true)));
    CHECK(feed(h, page(100, 40, NULL, 0, false)));
    CHECK(listed(h, 0) && history_user_count(h) == 140);
    history_destroy(h);

    /* A large directory stops at the local limit without duplicates. */
    h = history_create(auth);
    CHECK(h);
    int pages = 0;
    while (!history_users_complete(h) && pages < 100) {
        CHECK(feed(h, page(pages * 100, 100, NULL, 0, pages < 99)));
        ++pages;
    }
    CHECK(h->users_partial && history_user_count(h) == json_object_size(h->listed));
    CHECK(history_user_count(h) > 0 && history_user_count(h) < 10000);
    CHECK(!strcmp(history_users_notice(h), _("Lista parcial: limite local atingido.")));
    history_destroy(h);
    auth_destroy(auth);
    return 0;
}
