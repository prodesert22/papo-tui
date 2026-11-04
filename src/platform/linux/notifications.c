#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "app/desktop.h"
#include "net/clock.h"
#include "platform/linux/terminal_focus.h"
#include "platform/terminal_notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_GIO
#include <gio/gio.h>
#define NOTIFY_RETRY_MS 15000
struct slot {
    char key[2120], title[1025], body[2049], target[DESKTOP_TARGET_BYTES];
    unsigned count, id;
    uint64_t due, first, serial;
    bool busy, terminal; /* terminal: id belongs to the terminal, not D-Bus. */
};
struct shown {
    unsigned id;
    char key[80], target[DESKTOP_TARGET_BYTES];
    bool terminal;
};
struct desktop_notifier {
    GMainContext *context;
    GDBusConnection *bus;
    GCancellable *cancel;
    struct slot slots[64];
    unsigned refs, subscription, action_subscription, terminal_next;
    struct shown shown[32];
    size_t shown_next;
    char clicked_key[80], clicked_target[DESKTOP_TARGET_BYTES];
    bool clicked, clicked_terminal, raise;
    uint64_t epoch;
    bool dying, ready, markup, probing, attempted, connecting;
    char notice[128];
};
struct delivery {
    char key[80], target[DESKTOP_TARGET_BYTES];
    desktop_notifier *n;
    size_t slot;
    uint64_t serial, epoch;
};
struct capability_request {
    desktop_notifier *n;
    uint64_t epoch;
};
static void release(desktop_notifier *n)
{
    if (--n->refs)
        return;
    g_clear_object(&n->bus);
    g_clear_object(&n->cancel);
    g_main_context_unref(n->context);
    free(n);
}
static void unavailable(desktop_notifier *n)
{
    n->ready = false;
    if (n->attempted)
        snprintf(n->notice, sizeof(n->notice), "Avisos desktop indisponíveis; central mantida.");
}
static void close_notification(desktop_notifier *n, unsigned id)
{
    if (!n->bus || !id)
        return;
    g_dbus_connection_call(n->bus, "org.freedesktop.Notifications",
                           "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                           "CloseNotification", g_variant_new("(u)", id), NULL,
                           G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, NULL, NULL);
}
static void capabilities(GObject *source, GAsyncResult *result, gpointer context)
{
    struct capability_request *request = context;
    desktop_notifier *n = request->n;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (n->epoch == request->epoch)
        n->probing = false;
    if (!n->dying && n->epoch == request->epoch) {
        if (!reply)
            unavailable(n);
        else {
            gchar **caps = NULL;
            g_variant_get(reply, "(^as)", &caps);
            n->markup = false;
            for (size_t i = 0; caps && caps[i]; ++i)
                if (!strcmp(caps[i], "body-markup"))
                    n->markup = true;
            g_strfreev(caps);
            n->ready = true;
            n->notice[0] = 0;
        }
    }
    if (reply)
        g_variant_unref(reply);
    g_clear_error(&error);
    free(request);
    release(n);
}
static void probe(desktop_notifier *n)
{
    if (!n->bus || n->probing || n->dying)
        return;
    struct capability_request *request = malloc(sizeof(*request));
    if (!request)
        return;
    *request = (struct capability_request){n, n->epoch};
    n->probing = true;
    ++n->refs;
    g_main_context_push_thread_default(n->context);
    g_dbus_connection_call(n->bus, "org.freedesktop.Notifications",
                           "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                           "GetCapabilities", NULL, G_VARIANT_TYPE("(as)"),
                           G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, n->cancel, capabilities, request);
    g_main_context_pop_thread_default(n->context);
}
static void owner_changed(GDBusConnection *bus, const gchar *sender, const gchar *path,
                          const gchar *iface, const gchar *signal, GVariant *parameters,
                          gpointer context)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    desktop_notifier *n = context;
    const char *name, *old, *owner;
    g_variant_get(parameters, "(&s&s&s)", &name, &old, &owner);
    (void)name;
    (void)old;
    if (n->dying)
        return;
    ++n->epoch;
    n->ready = n->probing = false;
    for (size_t i = 0; i < 64; ++i) {
        if (n->slots[i].terminal)
            continue; /* Shown by the terminal, unaffected by the daemon. */
        n->slots[i].id = n->slots[i].count = 0;
        ++n->slots[i].serial;
    }
    if (*owner)
        probe(n);
}
static void action_invoked(GDBusConnection *bus, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *signal, GVariant *parameters,
                           gpointer context)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    desktop_notifier *n = context;
    guint id;
    const char *action;
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(us)")))
        return;
    g_variant_get(parameters, "(u&s)", &id, &action);
    if (n->dying || !id || strcmp(action, "default"))
        return;
    for (size_t i = 0; i < 32; ++i)
        if (!n->shown[i].terminal && n->shown[i].id == id) {
            snprintf(n->clicked_key, sizeof(n->clicked_key), "%s", n->shown[i].key);
            snprintf(n->clicked_target, sizeof(n->clicked_target), "%s", n->shown[i].target);
            n->clicked = true;
            n->clicked_terminal = false;
            return;
        }
}
static void bus_ready(GObject *source, GAsyncResult *result, gpointer context);
static void connect_bus(desktop_notifier *n)
{
    if (n->bus || n->connecting || n->dying)
        return;
    n->connecting = true;
    ++n->refs;
    g_main_context_push_thread_default(n->context);
    g_bus_get(G_BUS_TYPE_SESSION, n->cancel, bus_ready, n);
    g_main_context_pop_thread_default(n->context);
}
static void signal_released(gpointer context)
{
    release(context);
}
static void bus_ready(GObject *source, GAsyncResult *result, gpointer context)
{
    (void)source;
    desktop_notifier *n = context;
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_finish(result, &error);
    n->connecting = false;
    if (!n->dying && bus) {
        n->bus = bus;
        ++n->refs;
        g_main_context_push_thread_default(n->context);
        n->subscription = g_dbus_connection_signal_subscribe(
            bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
            "/org/freedesktop/DBus", "org.freedesktop.Notifications", G_DBUS_SIGNAL_FLAGS_NONE,
            owner_changed, n, signal_released);
        ++n->refs;
        n->action_subscription = g_dbus_connection_signal_subscribe(
            bus, NULL, "org.freedesktop.Notifications", "ActionInvoked",
            "/org/freedesktop/Notifications", NULL, G_DBUS_SIGNAL_FLAGS_NONE, action_invoked, n,
            signal_released);
        g_main_context_pop_thread_default(n->context);
        probe(n);
    } else {
        if (bus)
            g_object_unref(bus);
        if (!n->dying)
            unavailable(n);
    }
    g_clear_error(&error);
    release(n);
}
desktop_notifier *desktop_create(void)
{
    desktop_notifier *n = calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    n->refs = 1;
    n->context = g_main_context_new();
    n->cancel = g_cancellable_new();
    connect_bus(n);
    return n;
}
static void utf8_copy(char *out, size_t capacity, const char *source)
{
    if (!source || !g_utf8_validate(source, -1, NULL))
        source = "Texto indisponível";
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
        while (length && ((unsigned char)source[length] & 0xc0) == 0x80)
            --length;
    }
    memcpy(out, source, length);
    out[length] = 0;
}
void desktop_queue(desktop_notifier *n, const char *key, const char *title, const char *body,
                   const char *target)
{
    if (!n || n->dying || !key || strlen(key) >= sizeof(n->slots[0].key))
        return;
    n->attempted = true;
    uint64_t now;
    if (!net_now_ms(&now))
        return;
    size_t at = 64;
    for (size_t i = 0; i < 64; ++i)
        if (!strcmp(n->slots[i].key, key)) {
            at = i;
            break;
        }
    if (at == 64)
        for (size_t i = 0; i < 64; ++i)
            if (!*n->slots[i].key && !n->slots[i].busy) {
                at = i;
                break;
            }
    if (at == 64)
        return;
    struct slot *s = &n->slots[at];
    if (!s->count) {
        s->due = now; /* Instances group and recheck eligibility for 2s. */
        s->first = now;
    }
    snprintf(s->key, sizeof(s->key), "%s", key);
    utf8_copy(s->title, sizeof(s->title), title);
    utf8_copy(s->body, sizeof(s->body), body);
    snprintf(s->target, sizeof(s->target), "%s", target ? target : "");
    if (s->count < 1000000)
        ++s->count;
    if (!n->ready && !terminal_notify_ready()) {
        connect_bus(n);
        probe(n);
    }
}
static void sent(GObject *source, GAsyncResult *result, gpointer context)
{
    struct delivery *d = context;
    desktop_notifier *n = d->n;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    struct slot *s = &n->slots[d->slot];
    if (reply) {
        guint id;
        g_variant_get(reply, "(u)", &id);
        if (!n->dying && s->serial == d->serial && n->epoch == d->epoch) {
            s->id = id;
            s->terminal = false;
            struct shown *entry = &n->shown[n->shown_next++ % 32];
            entry->id = id;
            entry->terminal = false;
            snprintf(entry->key, sizeof(entry->key), "%s", d->key);
            snprintf(entry->target, sizeof(entry->target), "%s", d->target);
        }
        else if (!n->dying && n->epoch == d->epoch)
            close_notification(n, id);
        g_variant_unref(reply);
    } else if (!n->dying && n->epoch == d->epoch) {
        unavailable(n);
        /* A transient failure (daemon asleep or restarting) retries once the
         * capability probe succeeds, within the bounded window in poll. */
        if (s->serial == d->serial && !s->count && *s->key) {
            s->count = 1;
            s->due = 0;
        }
    }
    s->busy = false;
    g_clear_error(&error);
    free(d);
    release(n);
}
void desktop_cancel(desktop_notifier *n, const char *key)
{
    if (!n)
        return;
    for (size_t i = 0; i < 64; ++i)
        if (!key || !strcmp(n->slots[i].key, key)) {
            struct slot *s = &n->slots[i];
            if (s->terminal)
                terminal_notify_close(s->id);
            else
                close_notification(n, s->id);
            s->id = s->count = 0;
            s->terminal = false;
            s->key[0] = 0;
            ++s->serial;
        }
}
bool desktop_poll(desktop_notifier *n)
{
    if (!n)
        return false;
    char old[128];
    snprintf(old, sizeof(old), "%s", n->notice);
    for (int i = 0; i < 32 && g_main_context_pending(n->context); ++i)
        g_main_context_iteration(n->context, FALSE);
    uint64_t now;
    if (!net_now_ms(&now))
        return false;
    for (size_t i = 0; i < 64; ++i) {
        struct slot *s = &n->slots[i];
        if (!s->count || s->busy || now < s->due)
            continue;
        if (terminal_notify_ready()) {
            /* The terminal shows it, so a click focuses its window and tab. */
            if (!++n->terminal_next)
                n->terminal_next = 1;
            unsigned count = s->count, id = n->terminal_next;
            s->count = 0;
            char body[2200];
            snprintf(body, sizeof(body), count > 1 ? "%s · %u novos avisos" : "%s", s->body, count);
            if (!terminal_notify_send(id, s->title, body))
                continue;
            s->id = id;
            s->terminal = true;
            struct shown *entry = &n->shown[n->shown_next++ % 32];
            *entry = (struct shown){.id = id, .terminal = true};
            snprintf(entry->key, sizeof(entry->key), "%.79s", s->key);
            snprintf(entry->target, sizeof(entry->target), "%s", s->target);
            n->notice[0] = 0;
            continue;
        }
        if (!n->ready) {
            /* Keep it only briefly: probe or reconnect now, and deliver when
             * the daemon answers. A longer outage is not replayed. */
            if (now - s->first > NOTIFY_RETRY_MS)
                s->count = 0;
            else {
                connect_bus(n);
                probe(n);
            }
            continue;
        }
        unsigned count = s->count;
        s->count = 0;
        struct delivery *d = malloc(sizeof(*d));
        if (!d)
            continue;
        char body[2200];
        snprintf(body, sizeof(body), count > 1 ? "%s\n%u novos avisos" : "%s", s->body, count);
        char *escaped = n->markup ? g_markup_escape_text(body, -1) : NULL;
        const char *actions[] = {"default", "Abrir", NULL};
        GVariantBuilder hints;
        g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(1));
        *d = (struct delivery){.n = n, .slot = i, .serial = s->serial, .epoch = n->epoch};
        snprintf(d->key, sizeof(d->key), "%.79s", s->key);
        snprintf(d->target, sizeof(d->target), "%s", s->target);
        ++n->refs;
        s->busy = true;
        g_main_context_push_thread_default(n->context);
        g_dbus_connection_call(
            n->bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify",
            /* Never replace the previous id: a daemon that updates a dismissed
             * or already shown notification in place raises no new popup. */
            g_variant_new("(susss@as@a{sv}i)", "Papo TUI", 0u, "mail-unread", s->title,
                          escaped ? escaped : body, g_variant_new_strv(actions, -1),
                          g_variant_builder_end(&hints), -1),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, n->cancel, sent, d);
        g_main_context_pop_thread_default(n->context);
        g_free(escaped);
    }
    return strcmp(old, n->notice) != 0;
}
const char *desktop_notice(const desktop_notifier *n)
{
    return n ? n->notice : "";
}
bool desktop_take_activation(desktop_notifier *n, char key[80], char target[DESKTOP_TARGET_BYTES])
{
    if (!n)
        return false;
    unsigned id;
    while (!n->clicked && terminal_notify_take_activation(&id))
        for (size_t i = 0; i < 32; ++i)
            if (n->shown[i].terminal && n->shown[i].id == id) {
                snprintf(n->clicked_key, sizeof(n->clicked_key), "%s", n->shown[i].key);
                snprintf(n->clicked_target, sizeof(n->clicked_target), "%s", n->shown[i].target);
                n->clicked = true;
                n->clicked_terminal = true;
                break;
            }
    if (!n->clicked)
        return false;
    n->clicked = false;
    n->raise = !n->clicked_terminal;
    snprintf(key, 80, "%s", n->clicked_key);
    snprintf(target, DESKTOP_TARGET_BYTES, "%s", n->clicked_target);
    return true;
}
void desktop_raise_terminal(desktop_notifier *n, const char *title)
{
    if (!n || !n->raise || n->dying)
        return;
    n->raise = false;
    terminal_focus_request(n->bus, title);
}
void desktop_destroy(desktop_notifier *n)
{
    if (!n)
        return;
    desktop_cancel(n, NULL);
    n->dying = true;
    g_cancellable_cancel(n->cancel);
    if (n->subscription)
        g_dbus_connection_signal_unsubscribe(n->bus, n->subscription);
    if (n->action_subscription)
        g_dbus_connection_signal_unsubscribe(n->bus, n->action_subscription);
    /* Cancellation callbacks release retained owners; no blocking bus calls. */
    uint64_t start = 0, now = 0;
    net_now_ms(&start);
    do {
        for (int i = 0; i < 64 && g_main_context_pending(n->context); ++i)
            g_main_context_iteration(n->context, FALSE);
        if (n->refs == 1 || !net_now_ms(&now) || now - start >= 250)
            break;
        const struct timespec interval = {.tv_nsec = 1000000};
        nanosleep(&interval, NULL);
    } while (true);
    release(n);
}
#else
struct desktop_notifier {
    bool attempted;
};
desktop_notifier *desktop_create(void)
{
    return calloc(1, sizeof(desktop_notifier));
}
void desktop_destroy(desktop_notifier *n)
{
    free(n);
}
void desktop_queue(desktop_notifier *n, const char *key, const char *title, const char *body,
                   const char *target)
{
    if (n)
        n->attempted = true;
    (void)key;
    (void)title;
    (void)body;
}
void desktop_cancel(desktop_notifier *n, const char *key)
{
    (void)n;
    (void)key;
}
bool desktop_poll(desktop_notifier *n)
{
    (void)n;
    return false;
}
bool desktop_take_activation(desktop_notifier *n, char key[80], char target[DESKTOP_TARGET_BYTES])
{
    (void)n;
    (void)key;
    (void)target;
    return false;
}
void desktop_raise_terminal(desktop_notifier *n, const char *title)
{
    (void)n;
    (void)title;
}
const char *desktop_notice(const desktop_notifier *n)
{
    return n && n->attempted ? "Desktop desabilitado neste build; central disponível." : "";
}
#endif
