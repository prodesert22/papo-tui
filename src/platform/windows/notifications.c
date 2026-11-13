#include "app/desktop.h"
#include "model/text/translation.h"
#include "net/clock.h"
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
struct alert {
    char key[2120], title[1025], body[2049], target[DESKTOP_TARGET_BYTES];
    bool pending;
};
struct desktop_notifier {
    GApplication *application;
    GMainContext *context;
    struct alert alerts[64];
    unsigned refs;
    bool ready, busy, dying;
    char notice[128];
    /* GLib shows one notification balloon at a time from a single tray icon,
     * and a click (GLib 2.89.4 or newer) activates the application without
     * telling which notification it was: it opens the last one delivered,
     * unless that one was withdrawn since. */
    char shown_key[80], shown_target[DESKTOP_TARGET_BYTES];
    bool shown, clicked, raise;
    char clicked_key[80], clicked_target[DESKTOP_TARGET_BYTES];
};
typedef struct {
    desktop_notifier *owner;
    struct alert alert;
} delivery;
static void release(desktop_notifier *n)
{
    if (--n->refs)
        return;
    g_clear_object(&n->application);
    g_main_context_unref(n->context);
    free(n);
}
static void deliver(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    (void)cancel;
    delivery *job = data;
    GNotification *notification = g_notification_new(job->alert.title);
    g_notification_set_body(notification, job->alert.body);
    g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
    g_application_send_notification(job->owner->application, job->alert.key, notification);
    g_object_unref(notification);
    g_task_return_boolean(task, TRUE);
}
static void finished(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    delivery *job = g_task_get_task_data(G_TASK(result));
    desktop_notifier *n = job->owner;
    n->busy = false;
    if (!n->dying) {
        bool retained = false;
        for (size_t i = 0; i < 64; ++i)
            retained |= !strcmp(n->alerts[i].key, job->alert.key);
        if (!retained)
            g_application_withdraw_notification(n->application, job->alert.key);
        else {
            snprintf(n->shown_key, sizeof(n->shown_key), "%s", job->alert.key);
            snprintf(n->shown_target, sizeof(n->shown_target), "%s", job->alert.target);
            n->shown = true;
        }
    }
}
static void delivery_free(gpointer data)
{
    delivery *job = data;
    release(job->owner);
    free(job);
}
/* Runs on the main thread, from desktop_poll: GLib's Windows backend hands
 * a click on the notification or its tray icon to the default main context. */
static void activated(GApplication *application, gpointer data)
{
    (void)application;
    desktop_notifier *n = data;
    if (n->dying)
        return;
    snprintf(n->clicked_key, sizeof(n->clicked_key), "%s", n->shown ? n->shown_key : "");
    snprintf(n->clicked_target, sizeof(n->clicked_target), "%s", n->shown ? n->shown_target : "");
    n->clicked = true;
}
desktop_notifier *desktop_create(void)
{
    desktop_notifier *n = calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    n->refs = 1;
    n->context = g_main_context_new();
#ifdef HAVE_GIO
    n->application = g_application_new("io.github.prodesert22.PapoTUI", G_APPLICATION_NON_UNIQUE);
    GError *error = NULL;
    n->ready = g_application_register(n->application, NULL, &error);
    g_clear_error(&error);
    /* Without a handler, GLib would also print a warning over the interface. */
    g_signal_connect(n->application, "activate", G_CALLBACK(activated), n);
#endif
    return n;
}
static void copy_utf8(char *out, size_t capacity, const char *source)
{
    if (!source || !g_utf8_validate(source, -1, NULL))
        source = _("Texto indisponível");
    size_t size = strlen(source);
    if (size >= capacity) {
        size = capacity - 1;
        while (size && ((unsigned char)source[size] & 0xc0) == 0x80)
            --size;
    }
    memcpy(out, source, size);
    out[size] = 0;
}
void desktop_queue(desktop_notifier *n, const char *key, const char *title, const char *body,
                   const char *target)
{
    if (!n || n->dying || !key || strlen(key) >= sizeof(n->alerts[0].key))
        return;
    if (!n->ready) {
        snprintf(n->notice, sizeof(n->notice), _("Avisos desktop indisponíveis; central mantida."));
        return;
    }
    size_t at = 64;
    for (size_t i = 0; i < 64; ++i)
        if (!strcmp(n->alerts[i].key, key)) {
            at = i;
            break;
        }
    if (at == 64)
        for (size_t i = 0; i < 64; ++i)
            if (!*n->alerts[i].key) {
                at = i;
                break;
            }
    if (at == 64)
        return;
    snprintf(n->alerts[at].key, sizeof(n->alerts[at].key), "%s", key);
    copy_utf8(n->alerts[at].title, sizeof(n->alerts[at].title), title);
    copy_utf8(n->alerts[at].body, sizeof(n->alerts[at].body), body);
    snprintf(n->alerts[at].target, sizeof(n->alerts[at].target), "%s", target ? target : "");
    n->alerts[at].pending = true;
}
void desktop_cancel(desktop_notifier *n, const char *key)
{
    if (!n)
        return;
    for (size_t i = 0; i < 64; ++i)
        if (!key || !strcmp(n->alerts[i].key, key)) {
            if (n->application && *n->alerts[i].key)
                g_application_withdraw_notification(n->application, n->alerts[i].key);
            memset(&n->alerts[i], 0, sizeof(n->alerts[i]));
        }
    /* GLib cannot take a balloon back: a later click only raises the terminal. */
    if (!key || !strcmp(n->shown_key, key))
        n->shown = false;
}
bool desktop_poll(desktop_notifier *n)
{
    if (!n)
        return false;
    bool changed = false;
    for (unsigned i = 0; i < 32 && g_main_context_pending(n->context); ++i) {
        g_main_context_iteration(n->context, FALSE);
        changed = true;
    }
    /* Clicks arrive on the default context, which nothing else runs here. */
    for (unsigned i = 0; n->ready && i < 16 && g_main_context_pending(NULL); ++i)
        g_main_context_iteration(NULL, FALSE);
    changed |= n->clicked;
    if (!n->busy && !n->dying && n->ready)
        for (size_t i = 0; i < 64; ++i)
            if (n->alerts[i].pending) {
                delivery *job = malloc(sizeof(*job));
                if (!job)
                    break;
                job->owner = n;
                job->alert = n->alerts[i];
                ++n->refs;
                n->alerts[i].pending = false;
                n->busy = true;
                g_main_context_push_thread_default(n->context);
                GTask *task = g_task_new(NULL, NULL, finished, NULL);
                g_task_set_task_data(task, job, delivery_free);
                g_task_run_in_thread(task, deliver);
                g_object_unref(task);
                g_main_context_pop_thread_default(n->context);
                break;
            }
    return changed;
}
const char *desktop_notice(const desktop_notifier *n)
{
    return n ? n->notice : "";
}
void desktop_destroy(desktop_notifier *n)
{
    if (!n)
        return;
    desktop_cancel(n, NULL);
    if (n->application)
        g_signal_handlers_disconnect_by_data(n->application, n);
    gint64 end = g_get_monotonic_time() + 500000;
    while (n->busy && g_get_monotonic_time() < end) {
        desktop_poll(n);
        g_usleep(1000);
    }
    n->dying = true;
    release(n);
}

bool desktop_take_activation(desktop_notifier *n, char key[80], char target[DESKTOP_TARGET_BYTES])
{
    if (!n || !n->clicked)
        return false;
    n->clicked = false;
    n->raise = true;
    snprintf(key, 80, "%s", n->clicked_key);
    snprintf(target, DESKTOP_TARGET_BYTES, "%s", n->clicked_target);
    return true;
}
/* The console window under conhost; under Windows Terminal and other ConPTY
 * hosts, a hidden pseudo window owned by the terminal's own window. Clicking
 * the tray notification lets this process take the foreground; when Windows
 * refuses it anyway, the taskbar button flashes instead. The terminal keeps
 * whichever of its tabs was selected. */
void desktop_raise_terminal(desktop_notifier *n, const char *title)
{
    (void)title;
    if (!n || !n->raise || n->dying)
        return;
    n->raise = false;
    HWND console = GetConsoleWindow();
    if (!console)
        return;
    HWND window = GetAncestor(console, GA_ROOTOWNER);
    if (!window)
        window = console;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    if (!SetForegroundWindow(window)) {
        FLASHWINFO flash = {
            .cbSize = sizeof(flash), .hwnd = window, .dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG};
        FlashWindowEx(&flash);
    }
}
