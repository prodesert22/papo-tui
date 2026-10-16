#include "app/desktop.h"
#include "net/clock.h"
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct alert {
    char key[2120], title[1025], body[2049];
    bool pending;
};
struct desktop_notifier {
    GApplication *application;
    GMainContext *context;
    struct alert alerts[64];
    unsigned refs;
    bool ready, busy, dying;
    char notice[128];
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
    job->owner->busy = false;
    if (!job->owner->dying) {
        bool retained = false;
        for (size_t i = 0; i < 64; ++i)
            retained |= !strcmp(job->owner->alerts[i].key, job->alert.key);
        if (!retained)
            g_application_withdraw_notification(job->owner->application, job->alert.key);
    }
}
static void delivery_free(gpointer data)
{
    delivery *job = data;
    release(job->owner);
    free(job);
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
#endif
    return n;
}
static void copy_utf8(char *out, size_t capacity, const char *source)
{
    if (!source || !g_utf8_validate(source, -1, NULL))
        source = "Texto indisponível";
    size_t size = strlen(source);
    if (size >= capacity) {
        size = capacity - 1;
        while (size && ((unsigned char)source[size] & 0xc0) == 0x80)
            --size;
    }
    memcpy(out, source, size);
    out[size] = 0;
}
void desktop_queue(desktop_notifier *n, const char *key, const char *title, const char *body)
{
    if (!n || n->dying || !key || strlen(key) >= sizeof(n->alerts[0].key))
        return;
    if (!n->ready) {
        snprintf(n->notice, sizeof(n->notice), "Avisos desktop indisponíveis; central mantida.");
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
    gint64 end = g_get_monotonic_time() + 500000;
    while (n->busy && g_get_monotonic_time() < end) {
        desktop_poll(n);
        g_usleep(1000);
    }
    n->dying = true;
    release(n);
}
