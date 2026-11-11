#define _POSIX_C_SOURCE 200809L
/* The authors of replied-to messages kept outside the loaded window keep their
 * names and are requested even when only a placeholder of them is known. */
#include "model/conversation/directory.c"
#include "model/conversation/interactions.c"
#include "net/clock.h"
#include <assert.h>

static const char *original = "00000000-0000-4000-8000-000000000301";
static const char *old_author = "00000000-0000-4000-8000-000000000302";
static const char *placeholder = "00000000-0000-4000-8000-000000000303";
static const char *role = "00000000-0000-4000-8000-000000000304";
static const char *other = "00000000-0000-4000-8000-000000000305";

static void reference(history *h, const char *id, const char *author)
{
    json_t *message =
        json_pack("{s:s,s:s,s:s}", "id", id, "author_id", author, "content", "Antiga");
    assert(message && !json_object_set_new(actions(h)->references, id, message));
}

static bool collected(history *h, const char *id)
{
    json_t *ids = json_array(), *seen = json_object();
    assert(ids && seen);
    history_internal_actions_users(h, ids, seen);
    bool found = json_object_get(seen, id) != NULL;
    json_decref(ids);
    json_decref(seen);
    return found;
}

int main(void)
{
    auth_client *auth = auth_create();
    assert(auth);
    history *h = history_create(auth);
    assert(h && actions(h));

    /* The author of a referenced message survives the five-minute eviction. */
    reference(h, original, old_author);
    json_t *user = json_pack("{s:s,s:s,s:[]}", "id", old_author, "username", "antiga", "roles");
    assert(user && history_internal_cache_user(h, user, false));
    json_decref(user);
    uint64_t now = 0;
    assert(net_now_ms(&now));
    history_maintain(h, now + 301000);
    assert(!strcmp(history_author(h, old_author), "antiga"));

    /* A role event leaves only a nameless placeholder: it is still requested. */
    json_t *event =
        json_pack("{s:s,s:s,s:s}", "type", "role_add", "user_id", placeholder, "role_id", role);
    assert(event && history_internal_role_event(h, event));
    json_decref(event);
    assert(json_object_get(h->users, placeholder));
    reference(h, other, placeholder);
    assert(collected(h, placeholder));
    assert(!collected(h, old_author)); /* Already resolved. */

    history_destroy(h);
    auth_destroy(auth);
    return 0;
}
