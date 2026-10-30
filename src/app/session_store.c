#include "app/session_store.h"
#include "app/session_slot.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One slot per instance URL. Each slot owns its marker and credential and
 * keeps the commit protocol of the platform backend; this layer only decides
 * which borrowed client each slot watches. */
typedef struct {
    session_slot *slot;
    unsigned sequence;
    auth_client *client;
} session_entry;
struct session_store {
    const profile_config *config;
    session_scope *scope;
    session_entry entries[SESSION_SLOT_LIMIT];
    size_t count;
    unsigned next;
    char notice[192];
};

session_store *session_store_create(const profile_config *config, bool restore)
{
    session_store *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->config = config;
    s->next = 1;
    s->scope = session_scope_open(config, s->notice, sizeof(s->notice));
    if (!s->scope)
        return s;
    unsigned sequences[SESSION_SLOT_LIMIT];
    size_t count = session_scope_list(s->scope, sequences, SESSION_SLOT_LIMIT);
    for (size_t i = 0; i < count; ++i) {
        session_slot *slot = session_slot_create(config, sequences[i], restore);
        if (!slot)
            continue;
        s->entries[s->count++] = (session_entry){slot, sequences[i], NULL};
        if (sequences[i] >= s->next)
            s->next = sequences[i] + 1;
    }
    return s;
}
static session_entry *entry_of(session_store *s, const auth_client *client)
{
    for (size_t i = 0; i < s->count; ++i)
        if (s->entries[i].client == client)
            return &s->entries[i];
    return NULL;
}
static session_entry *entry_for(session_store *s, const char *url)
{
    if (!url || !*url)
        return NULL;
    for (size_t i = 0; i < s->count; ++i)
        if (!strcmp(session_slot_url(s->entries[i].slot), url))
            return &s->entries[i];
    return NULL;
}
static void release_entry(session_entry *e)
{
    if (e && e->client) {
        session_slot_unwatch(e->slot, e->client);
        e->client = NULL;
    }
}
/* Binds client to the slot of its URL, creating one when allowed. A newer
 * login to the same URL replaces the older client, as its tab does. */
static void attach_client(session_store *s, auth_client *client, bool create)
{
    const char *url = auth_instance_url(client);
    session_entry *current = entry_of(s, client), *target = entry_for(s, url);
    if (current && (current == target || !*session_slot_url(current->slot)))
        return;
    if (!target && create && s->scope && s->count < SESSION_SLOT_LIMIT && s->next) {
        session_slot *slot = session_slot_create(s->config, s->next, false);
        if (slot) {
            target = &s->entries[s->count++];
            *target = (session_entry){slot, s->next, NULL};
            s->next = s->next < 0xffffffffu ? s->next + 1 : 0;
        }
    }
    if (!target)
        return;
    release_entry(current);
    if (target->client != client)
        release_entry(target);
    target->client = client;
    /* Install the rotation gate before the client can refresh its session. */
    session_slot_poll(target->slot, client);
}
bool session_store_poll(session_store *s, auth_client *active)
{
    if (!s)
        return false;
    bool changed = false;
    /* Pending login/add forms never replace a tab before they have a session. */
    if (active && auth_has_session(active))
        attach_client(s, active, true);
    for (size_t i = 0; i < s->count; ++i) {
        session_entry *e = &s->entries[i];
        const char *url = session_slot_url(e->slot);
        /* A client signed into another instance after a logout belongs elsewhere. */
        if (e->client && auth_has_session(e->client) && *url &&
            strcmp(url, auth_instance_url(e->client))) {
            auth_client *client = e->client;
            release_entry(e);
            attach_client(s, client, true);
        }
    }
    for (size_t i = 0; i < s->count; ++i)
        changed |= session_slot_poll(s->entries[i].slot, s->entries[i].client);
    return changed;
}
void session_store_watch(session_store *s, auth_client *client)
{
    if (s && client)
        attach_client(s, client, false);
}
void session_store_unwatch(session_store *s, auth_client *client)
{
    if (!s || !client)
        return;
    session_entry *e;
    while ((e = entry_of(s, client)))
        release_entry(e);
}
const char *session_store_last_url(const session_store *s)
{
    if (!s)
        return "";
    /* The newest saved instance opens first; otherwise remember the newest URL. */
    for (int saved = 1; saved >= 0; --saved)
        for (size_t i = s->count; i > 0; --i) {
            const session_slot *slot = s->entries[i - 1].slot;
            if (*session_slot_url(slot) && (!saved || session_slot_saved(slot)))
                return session_slot_url(slot);
        }
    return "";
}
const char *session_store_notice(const session_store *s)
{
    if (!s)
        return "";
    if (*s->notice)
        return s->notice;
    for (size_t i = 0; i < s->count; ++i)
        if (*session_slot_notice(s->entries[i].slot))
            return session_slot_notice(s->entries[i].slot);
    return "";
}
bool session_store_restoring(const session_store *s)
{
    if (s)
        for (size_t i = 0; i < s->count; ++i)
            if (session_slot_restoring(s->entries[i].slot))
                return true;
    return false;
}
char *session_store_take_resume(session_store *s, const char **url)
{
    if (url)
        *url = "";
    if (!s)
        return NULL;
    for (size_t i = 0; i < s->count; ++i) {
        char *resume = session_slot_take_resume(s->entries[i].slot);
        if (resume) {
            if (url)
                *url = session_slot_url(s->entries[i].slot);
            return resume;
        }
    }
    return NULL;
}
void session_store_cancel_resume(session_store *s)
{
    if (s)
        for (size_t i = 0; i < s->count; ++i)
            session_slot_cancel_resume(s->entries[i].slot);
}
unsigned session_store_order(const session_store *s, const auth_client *client)
{
    if (s)
        for (size_t i = 0; i < s->count; ++i)
            if (s->entries[i].client == client)
                return s->entries[i].sequence;
    return 0xffffffffu;
}
void session_store_destroy(session_store *s)
{
    if (!s)
        return;
    for (size_t i = 0; i < s->count; ++i)
        release_entry(&s->entries[i]);
    /* Drain every slot together instead of waiting for each in turn. */
    for (int round = 0; round < 2000; ++round) {
        bool busy = false;
        for (size_t i = 0; i < s->count; ++i) {
            session_slot_poll(s->entries[i].slot, NULL);
            busy |= session_slot_busy(s->entries[i].slot);
        }
        if (!busy)
            break;
        g_usleep(1000);
    }
    for (size_t i = 0; i < s->count; ++i)
        session_slot_destroy(s->entries[i].slot);
    session_scope_close(s->scope);
    free(s);
}
