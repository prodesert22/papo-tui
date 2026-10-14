#define _POSIX_C_SOURCE 200809L
#include "app/session_store.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_GIO
#include <gio/gio.h>
#endif
#ifdef HAVE_LIBSECRET
#include <libsecret/secret.h>
#endif

enum { PROVIDER_NONE, PROVIDER_SECRET, PROVIDER_WALLET6, PROVIDER_WALLET5 };
enum { OP_NONE, OP_LOOKUP, OP_SAVE, OP_DELETE };
typedef struct {
    char url[2049];
    unsigned provider;
    bool allowed;
    uint64_t revision;
} public_state;
struct session_store {
    int directory, lock;
    char file[96], scope[65], notice[192];
    public_state state, written;
    auth_client *active;
    uint64_t saved_generation;
    bool had_session, restore, available, dying, dirty, state_busy, state_failed;
    bool operation_busy, delete_wanted, gate_waiting, gate_failed;
    uint64_t operation_revision;
    int operation, provider;
    char *resume, *pending, *saved;
    unsigned refs;
#ifdef HAVE_GIO
    GMainContext *context;
    GCancellable *cancel;
    GDBusConnection *bus;
    int wallet_handle;
    guint wallet_signal;
    int wallet_transaction;
    bool wallet_signal_early;
    bool activatable_checked;
    unsigned folder_stage;
    int early_transaction, early_handle;
#endif
#ifdef HAVE_LIBSECRET
    SecretService *service;
    GHashTable *attributes;
#endif
};

static void notice(session_store *s, const char *message)
{ snprintf(s->notice, sizeof(s->notice), "%s", message); }
static bool private_file(int fd, mode_t mode)
{
    struct stat st;
    return fstat(fd, &st) == 0 && st.st_uid == getuid() &&
           (st.st_mode & 07777) == mode && st.st_nlink == 1 && S_ISREG(st.st_mode);
}
#ifdef HAVE_GIO
static int state_directory(void)
{
    const char *xdg = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    char path[4096];
    if (xdg && *xdg) {
        if (*xdg != '/' || snprintf(path, sizeof(path), "%s", xdg) >= (int)sizeof(path)) return -1;
    } else if (!home || *home != '/' || snprintf(path, sizeof(path), "%s/.local/state", home) >= (int)sizeof(path)) return -1;
    for (char *p = path + 1; *p; ++p) if (*p == '/') {
        *p = 0; int rc = mkdir(path, 0700); *p = '/'; if (rc && errno != EEXIST) return -1;
    }
    if (mkdir(path, 0700) && errno != EEXIST) return -1;
    int parent = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0) return -1;
    if (mkdirat(parent, "papo-tui", 0700) && errno != EEXIST) { close(parent); return -1; }
    int fd = openat(parent, "papo-tui", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    struct stat st;
    if (fd >= 0 && (fstat(fd, &st) || st.st_uid != getuid() || (st.st_mode & 07777) != 0700)) { close(fd); return -1; }
    return fd;
}
#endif
static bool write_state(int directory, const char *file, const public_state *state)
{
    struct stat existing;
    if (fstatat(directory, file, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(existing.st_mode) || existing.st_uid != getuid() || existing.st_nlink != 1 ||
            (existing.st_mode & 07777) != 0600) return false;
    } else if (errno != ENOENT) return false;
    json_t *obj = json_pack("{s:i,s:s,s:i,s:b,s:I}", "version", 1, "url", state->url,
                          "provider", (int)state->provider, "restore_allowed", state->allowed,
                          "revision", (json_int_t)state->revision);
    char *data = obj ? json_dumps(obj, JSON_COMPACT) : NULL;
    json_decref(obj);
    if (!data) return false;
    char temporary[128]; int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        snprintf(temporary, sizeof(temporary), "%s.%ld.%d.tmp", file, (long)getpid(), attempt);
        fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) break;
    }
    bool ok = fd >= 0;
    size_t length = strlen(data), written = 0;
    while (ok && written < length) {
        ssize_t n = write(fd, data + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) ok = false; else written += (size_t)n;
    }
    free(data);
    if (ok && fsync(fd)) ok = false;
    if (fd >= 0 && close(fd)) ok = false;
    if (ok && renameat(directory, temporary, directory, file)) ok = false;
    if (ok && fsync(directory)) ok = false;
    if (!ok && fd >= 0) unlinkat(directory, temporary, 0);
    return ok;
}
static bool read_state(session_store *s)
{
    int fd = openat(s->directory, s->file, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT;
    struct stat st;
    if (!private_file(fd, 0600) || fstat(fd, &st) || st.st_size > 8192) { close(fd); return false; }
    json_error_t error; json_t *obj = json_loadfd(fd, JSON_REJECT_DUPLICATES, &error); close(fd);
    json_t *url = json_object_get(obj, "url"), *provider = json_object_get(obj, "provider");
    json_t *allowed = json_object_get(obj, "restore_allowed"), *revision = json_object_get(obj, "revision");
    bool ok = json_is_object(obj) && json_object_size(obj) == 5 &&
              json_integer_value(json_object_get(obj, "version")) == 1 && json_is_string(url) &&
              json_string_length(url) < sizeof(s->state.url) && strlen(json_string_value(url)) == json_string_length(url) &&
              json_is_integer(provider) && json_integer_value(provider) >= 0 && json_integer_value(provider) <= PROVIDER_WALLET5 &&
              json_is_boolean(allowed) && json_is_integer(revision) && json_integer_value(revision) >= 0;
    if (ok) {
        const char *value = json_string_value(url); char message[192];
        char *canonical = *value ? auth_normalize_url(value, message, sizeof(message)) : NULL;
        if (*value && (!canonical || strcmp(canonical, value))) ok = false;
        curl_free(canonical);
        if (ok) {
            snprintf(s->state.url, sizeof(s->state.url), "%s", value);
            s->state.provider = (unsigned)json_integer_value(provider);
            s->state.allowed = json_is_true(allowed);
            s->state.revision = (uint64_t)json_integer_value(revision);
            s->written = s->state;
        }
    }
    json_decref(obj); return ok;
}
static void invalidate(session_store *s)
{
    s->state.allowed = false; ++s->state.revision; s->dirty = true;
    auth_session_free(&s->pending); auth_session_free(&s->saved);
}

#ifdef HAVE_GIO
static session_store *retain(session_store *s) { ++s->refs; return s; }
static void release(session_store *s);
static void start_operation(session_store *s);
static void unavailable(session_store *s)
{
    s->available = s->restore = false;
    notice(s, "Chaveiro indisponível. A sessão fica apenas nesta execução.");
}
typedef struct { int directory; char file[96]; public_state state; } state_job;
static void state_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source; (void)cancel; state_job *job = data;
    g_task_return_boolean(task, write_state(job->directory, job->file, &job->state));
}
static void state_job_free(gpointer data) { state_job *job = data; close(job->directory); free(job); }
static void state_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source; session_store *s = data; GError *error = NULL;
    state_job *job = g_task_get_task_data(G_TASK(result));
    bool ok = g_task_propagate_boolean(G_TASK(result), &error);
    g_clear_error(&error); s->state_busy = false;
    if (ok) s->written = job->state;
    else { s->state_failed = true; s->gate_failed = true; notice(s, "Não foi possível proteger o estado da sessão. Recuperação desativada."); }
    release(s);
}
static void schedule_state(session_store *s)
{
    if (s->state_busy || !s->dirty || s->state_failed || s->directory < 0) return;
    state_job *job = calloc(1, sizeof(*job));
    if (!job || (job->directory = dup(s->directory)) < 0) { free(job); s->state_failed = s->gate_failed = true; return; }
    job->state = s->state; snprintf(job->file, sizeof(job->file), "%s", s->file);
    g_main_context_push_thread_default(s->context);
    GTask *task = g_task_new(NULL, NULL, state_done, retain(s));
    g_task_set_task_data(task, job, state_job_free);
    s->dirty = false; s->state_busy = true;
    g_task_run_in_thread(task, state_worker); g_object_unref(task);
    g_main_context_pop_thread_default(s->context);
}
static void operation_done(session_store *s, bool ok, char *value)
{
    if (g_cancellable_is_cancelled(s->cancel)) ok = false;
    int operation = s->operation;
    s->operation = OP_NONE; s->operation_busy = false;
    if (!s->dying) {
        if (!ok) { unavailable(s); auth_session_free(&s->pending); }
        else if (operation == OP_LOOKUP) {
            if (!value && s->restore) invalidate(s);
            if (s->restore) { s->resume = value; value = NULL; }
            s->restore = false;
        } else if (operation == OP_SAVE && s->operation_revision == s->state.revision && !s->state_failed) {
            s->state.allowed = true; s->dirty = true;
        }
    }
    auth_session_free(&value);
}
#ifdef HAVE_LIBSECRET
static const SecretSchema schema = {
    "org.papo.TUI.Session", SECRET_SCHEMA_NONE,
    {{"application", SECRET_SCHEMA_ATTRIBUTE_STRING}, {"scope", SECRET_SCHEMA_ATTRIBUTE_STRING}, {NULL, 0}},
    0, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static void secret_done(GObject *source, GAsyncResult *result, gpointer data)
{
    session_store *s = data; GError *error = NULL; bool ok = true; char *copy = NULL;
    if (s->operation == OP_LOOKUP) {
        SecretValue *value = secret_service_lookup_finish(SECRET_SERVICE(source), result, &error);
        if (value) {
            gsize length = 0; const char *secret = secret_value_get(value, &length);
            if (length <= 16384 && (!length || !memchr(secret, 0, length))) {
                copy = malloc(length + 1); if (copy) { memcpy(copy, secret, length); copy[length] = 0; } else ok = false;
            } else ok = false;
            secret_value_unref(value);
        }
    } else if (s->operation == OP_SAVE) ok = secret_service_store_finish(SECRET_SERVICE(source), result, &error);
    else (void)secret_service_clear_finish(SECRET_SERVICE(source), result, &error);
    operation_done(s, ok && !error, copy); g_clear_error(&error); release(s);
}
#endif
static const char *wallet_name(session_store *s) { return s->provider == PROVIDER_WALLET6 ? "org.kde.kwalletd6" : "org.kde.kwalletd5"; }
static const char *wallet_path(session_store *s) { return s->provider == PROVIDER_WALLET6 ? "/modules/kwalletd6" : "/modules/kwalletd5"; }
static void wallet_done(GObject *source, GAsyncResult *result, gpointer data)
{
    session_store *s = data; GError *error = NULL; GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    char *copy = NULL; bool ok = reply && !error;
    if (ok && s->operation == OP_LOOKUP) {
        const char *text; g_variant_get(reply, "(&s)", &text);
        if (strlen(text) > 16384) ok = false;
        else if (*text) { copy = strdup(text); if (!copy) ok = false; }
    } else if (ok) { gint code; g_variant_get(reply, "(i)", &code); ok = code == 0; }
    if (reply) g_variant_unref(reply);
    operation_done(s, ok, copy); g_clear_error(&error); release(s);
}
static void start_operation(session_store *s)
{
    if (!s->available || s->operation_busy || s->dying) return;
    if (s->restore && s->written.allowed && s->written.provider == (unsigned)s->provider) s->operation = OP_LOOKUP;
    else if (s->delete_wanted) { s->operation = OP_DELETE; s->delete_wanted = false; }
    else if (s->pending && !s->state_busy && !s->dirty && !s->written.allowed && !s->state_failed) s->operation = OP_SAVE;
    else { s->restore = false; return; }
    s->operation_busy = true; s->operation_revision = s->state.revision;
    g_main_context_push_thread_default(s->context);
#ifdef HAVE_LIBSECRET
    if (s->provider == PROVIDER_SECRET) {
        if (s->operation == OP_LOOKUP) secret_service_lookup(s->service, &schema, s->attributes, s->cancel, secret_done, retain(s));
        else if (s->operation == OP_DELETE) secret_service_clear(s->service, &schema, s->attributes, s->cancel, secret_done, retain(s));
        else {
            SecretValue *value = secret_value_new(s->pending, -1, "application/json");
            secret_service_store(s->service, &schema, s->attributes, SECRET_COLLECTION_DEFAULT,
                                 "Papo TUI — última sessão", value, s->cancel, secret_done, retain(s));
            secret_value_unref(value);
        }
    } else
#endif
    {
        GVariant *args; const char *method; const GVariantType *type;
        if (s->operation == OP_LOOKUP) {
            method = "readPassword"; type = G_VARIANT_TYPE("(s)");
            args = g_variant_new("(isss)", s->wallet_handle, "Papo TUI", s->scope, "papo-tui");
        } else if (s->operation == OP_DELETE) {
            method = "removeEntry"; type = G_VARIANT_TYPE("(i)");
            args = g_variant_new("(isss)", s->wallet_handle, "Papo TUI", s->scope, "papo-tui");
        } else {
            method = "writePassword"; type = G_VARIANT_TYPE("(i)");
            args = g_variant_new("(issss)", s->wallet_handle, "Papo TUI", s->scope, s->pending, "papo-tui");
        }
        g_dbus_connection_call(s->bus, wallet_name(s), wallet_path(s), "org.kde.KWallet", method,
                               args, type, G_DBUS_CALL_FLAGS_NONE, 15000, s->cancel, wallet_done, retain(s));
    }
    if (s->operation == OP_SAVE) {
        auth_session_free(&s->saved); s->saved = s->pending; s->pending = NULL;
    }
    g_main_context_pop_thread_default(s->context);
}
static void wallet_folder_done(GObject *source, GAsyncResult *result, gpointer data)
{
    session_store *s = data; GError *error = NULL; GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    gboolean ok = false; if (reply) { g_variant_get(reply, "(b)", &ok); g_variant_unref(reply); }
    if (error || g_cancellable_is_cancelled(s->cancel)) unavailable(s);
    else if (ok) { s->available = true; start_operation(s); }
    else if (s->folder_stage < 2) {
        ++s->folder_stage;
        g_main_context_push_thread_default(s->context);
        g_dbus_connection_call(s->bus, wallet_name(s), wallet_path(s), "org.kde.KWallet",
                               s->folder_stage == 1 ? "createFolder" : "hasFolder",
                               g_variant_new("(iss)", s->wallet_handle, "Papo TUI", "papo-tui"), G_VARIANT_TYPE("(b)"),
                               G_DBUS_CALL_FLAGS_NONE, 15000, s->cancel, wallet_folder_done, retain(s));
        g_main_context_pop_thread_default(s->context);
    } else unavailable(s);
    g_clear_error(&error); release(s);
}
static void wallet_opened(session_store *s, int transaction, int handle)
{
    if (s->dying || g_cancellable_is_cancelled(s->cancel)) return;
    if (s->wallet_transaction < 0) { s->wallet_signal_early = true; s->early_transaction = transaction; s->early_handle = handle; return; }
    if (transaction != s->wallet_transaction) return;
    if (handle < 0) { unavailable(s); return; }
    s->wallet_handle = handle;
    s->folder_stage = 0;
    g_main_context_push_thread_default(s->context);
    g_dbus_connection_call(s->bus, wallet_name(s), wallet_path(s), "org.kde.KWallet", "hasFolder",
                           g_variant_new("(iss)", handle, "Papo TUI", "papo-tui"), G_VARIANT_TYPE("(b)"),
                           G_DBUS_CALL_FLAGS_NONE, 15000, s->cancel, wallet_folder_done, retain(s));
    g_main_context_pop_thread_default(s->context);
}
static void wallet_signal(GDBusConnection *bus, const gchar *sender, const gchar *path,
                          const gchar *iface, const gchar *signal, GVariant *parameters, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)iface; (void)signal;
    session_store *s = data; gint transaction, handle;
    g_variant_get(parameters, "(ii)", &transaction, &handle);
    if (!s->dying) wallet_opened(s, transaction, handle);
}
static void wallet_open_done(GObject *source, GAsyncResult *result, gpointer data)
{
    session_store *s = data; GError *error = NULL; GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!reply || error || g_cancellable_is_cancelled(s->cancel)) unavailable(s);
    else {
        g_variant_get(reply, "(i)", &s->wallet_transaction);
        if (s->wallet_transaction < 0) unavailable(s);
        else if (s->wallet_signal_early) wallet_opened(s, s->early_transaction, s->early_handle);
    }
    if (reply) g_variant_unref(reply);
    g_clear_error(&error); release(s);
}
static void wallet_name_done(GObject *source, GAsyncResult *result, gpointer data)
{
    session_store *s = data; GError *error = NULL; GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!reply || error || g_cancellable_is_cancelled(s->cancel)) unavailable(s);
    else {
        const char *name; g_variant_get(reply, "(&s)", &name);
        g_main_context_push_thread_default(s->context);
        s->wallet_signal = g_dbus_connection_signal_subscribe(s->bus, wallet_name(s), "org.kde.KWallet",
                           "walletAsyncOpened", wallet_path(s), NULL, G_DBUS_SIGNAL_FLAGS_NONE, wallet_signal, s, NULL);
        g_dbus_connection_call(s->bus, wallet_name(s), wallet_path(s), "org.kde.KWallet", "openAsync",
                               g_variant_new("(sxsb)", name, (gint64)0, "papo-tui", TRUE), G_VARIANT_TYPE("(i)"),
                               G_DBUS_CALL_FLAGS_NONE, 15000, s->cancel, wallet_open_done, retain(s));
        g_main_context_pop_thread_default(s->context);
    }
    if (reply) g_variant_unref(reply);
    g_clear_error(&error); release(s);
}
#ifdef HAVE_LIBSECRET
static void secret_open_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source; session_store *s = data; GError *error = NULL;
    s->service = secret_service_open_finish(result, &error);
    if (!s->service || error || g_cancellable_is_cancelled(s->cancel)) unavailable(s);
    else { s->available = true; start_operation(s); }
    g_clear_error(&error); release(s);
}
#endif
static void names_done(GObject *source, GAsyncResult *result, gpointer data)
{
    session_store *s = data; GError *error = NULL; GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    bool secret = false, wallet6 = false, wallet5 = false;
    if (reply) {
        gchar **names; g_variant_get(reply, "(^as)", &names);
        for (size_t i = 0; names[i]; ++i) {
            secret |= !strcmp(names[i], "org.freedesktop.secrets");
            wallet6 |= !strcmp(names[i], "org.kde.kwalletd6"); wallet5 |= !strcmp(names[i], "org.kde.kwalletd5");
        }
        g_strfreev(names); g_variant_unref(reply);
    }
    g_clear_error(&error);
    if (s->dying || g_cancellable_is_cancelled(s->cancel)) { unavailable(s); release(s); return; }
    if (!secret && !wallet6 && !wallet5 && !s->activatable_checked && !s->dying) {
        s->activatable_checked = true;
        g_main_context_push_thread_default(s->context);
        g_dbus_connection_call(s->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                              "ListActivatableNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 5000,
                              s->cancel, names_done, retain(s));
        g_main_context_pop_thread_default(s->context); release(s); return;
    }
    if (s->state.allowed && s->state.provider == PROVIDER_WALLET6 && wallet6) secret = false;
    if (s->state.allowed && s->state.provider == PROVIDER_WALLET5 && wallet5) { secret = false; wallet6 = false; }
    g_main_context_push_thread_default(s->context);
#ifdef HAVE_LIBSECRET
    if (secret) {
        s->provider = PROVIDER_SECRET;
        secret_service_open(SECRET_TYPE_SERVICE, "org.freedesktop.secrets", SECRET_SERVICE_OPEN_SESSION,
                            s->cancel, secret_open_done, retain(s));
    } else
#else
    (void)secret;
#endif
    if (wallet6 || wallet5) {
        s->provider = wallet6 ? PROVIDER_WALLET6 : PROVIDER_WALLET5;
        g_dbus_connection_call(s->bus, wallet_name(s), wallet_path(s), "org.kde.KWallet", "networkWallet",
                               NULL, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 15000, s->cancel, wallet_name_done, retain(s));
    } else unavailable(s);
    g_main_context_pop_thread_default(s->context); release(s);
}
static void bus_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source; session_store *s = data; GError *error = NULL;
    s->bus = g_bus_get_finish(result, &error);
    if (!s->bus || error || g_cancellable_is_cancelled(s->cancel)) unavailable(s);
    else {
        g_main_context_push_thread_default(s->context);
        g_dbus_connection_call(s->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                              "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 5000,
                              s->cancel, names_done, retain(s));
        g_main_context_pop_thread_default(s->context);
    }
    g_clear_error(&error); release(s);
}
#else
static void schedule_state(session_store *s)
{
    if (!s->dirty || s->directory < 0 || s->state_failed) return;
    s->dirty = false;
    if (write_state(s->directory, s->file, &s->state)) s->written = s->state;
    else s->state_failed = s->gate_failed = true;
}
#endif

static int rotation_gate(void *context, auth_client *client)
{
    session_store *s = context;
    if (s->active != client || s->directory < 0 || !*s->state.url) return 1;
    if (s->state_failed) return -1;
    if (!s->gate_waiting) { invalidate(s); s->gate_waiting = true; schedule_state(s); }
    if (s->gate_failed) return -1;
    if (s->state_busy || s->dirty || s->written.allowed || s->written.revision != s->state.revision) return 0;
    s->gate_waiting = false;
    return 1;
}
session_store *session_store_create(int config_directory, bool restore)
{
    session_store *s = calloc(1, sizeof(*s)); if (!s) return NULL;
    s->directory = s->lock = -1; s->refs = 1;
    struct stat config;
    if (fstat(config_directory, &config) || config.st_uid != getuid() || !S_ISDIR(config.st_mode) ||
        (config.st_mode & 07777) != 0700) goto disabled;
#ifdef HAVE_GIO
    char descriptor[64], path[4096];
    snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", config_directory);
    ssize_t n = readlink(descriptor, path, sizeof(path) - 1);
    if (n <= 0 || n >= (ssize_t)sizeof(path) - 1) goto disabled;
    path[n] = 0;
    char *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path, n);
    snprintf(s->scope, sizeof(s->scope), "%s", scope); g_free(scope);
    snprintf(s->file, sizeof(s->file), "session-%s.json", s->scope);
    s->directory = state_directory();
#else
    snprintf(s->scope, sizeof(s->scope), "%jx-%jx", (uintmax_t)config.st_dev, (uintmax_t)config.st_ino);
    snprintf(s->file, sizeof(s->file), "session.json");
    s->directory = dup(config_directory);
#endif
    if (s->directory < 0) goto disabled;
    char lock[96]; snprintf(lock, sizeof(lock), "session-%s.lock", s->scope);
    s->lock = openat(s->directory, lock, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (s->lock < 0 || !private_file(s->lock, 0600) || flock(s->lock, LOCK_EX | LOCK_NB)) {
        notice(s, "Outra janela usa a sessão salva. Esta janela exige login e não salvará sessão.");
        goto disabled;
    }
    if (!read_state(s)) { notice(s, "Estado de sessão inválido. Recuperação desativada."); s->state_failed = true; goto disabled; }
    s->restore = restore && s->state.allowed;
#ifdef HAVE_GIO
    s->context = g_main_context_new(); s->cancel = g_cancellable_new(); s->wallet_transaction = -1; s->wallet_handle = -1;
#ifdef HAVE_LIBSECRET
    s->attributes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(s->attributes, g_strdup("application"), g_strdup("papo-tui"));
    g_hash_table_insert(s->attributes, g_strdup("scope"), g_strdup(s->scope));
#endif
    g_main_context_push_thread_default(s->context);
    g_bus_get(G_BUS_TYPE_SESSION, s->cancel, bus_done, retain(s));
    g_main_context_pop_thread_default(s->context);
#else
    s->restore = false;
    notice(s, "Este build não oferece chaveiro. A sessão fica apenas nesta execução.");
#endif
    return s;
disabled:
    if (s->lock >= 0) { close(s->lock); s->lock = -1; }
    if (s->directory >= 0) { close(s->directory); s->directory = -1; }
    s->restore = false;
    if (!*s->notice) notice(s, "Não foi possível abrir o estado privado. A sessão fica apenas nesta execução.");
    return s;
}
bool session_store_poll(session_store *s, auth_client *active)
{
    if (!s) return false;
    public_state before = s->state; bool restoring = s->restore, available = s->available;
    char before_notice[sizeof(s->notice)]; memcpy(before_notice, s->notice, sizeof(before_notice));
#ifdef HAVE_GIO
    if (s->context) for (int i = 0; i < 32 && g_main_context_pending(s->context); ++i) g_main_context_iteration(s->context, FALSE);
#endif
    /* Pending login/add forms do not replace the last authenticated tab. Its
     * inactive refresh still has to update the one persistent credential. */
    if (s->active != active && (!s->active || (active && auth_has_session(active)))) {
        s->active = active; s->had_session = false; s->gate_waiting = s->gate_failed = false;
        auth_session_free(&s->pending); auth_session_free(&s->saved);
        if (active) auth_set_session_gate(active, rotation_gate, s);
    }
    active = s->active;
    if (active && auth_has_session(active) && (strcmp(s->state.url, auth_instance_url(active)) || !s->had_session)) {
        invalidate(s); s->had_session = true; s->saved_generation = auth_context_generation(active);
        snprintf(s->state.url, sizeof(s->state.url), "%s", auth_instance_url(active));
        s->state.provider = (unsigned)s->provider;
    }
    if (active && !auth_busy(active)) {
        if (auth_has_session(active)) {
            if (s->state.provider != (unsigned)s->provider) { invalidate(s); s->state.provider = (unsigned)s->provider; }
            bool changed = strcmp(s->state.url, auth_instance_url(active)) ||
                           !s->had_session || s->saved_generation != auth_context_generation(active);
            if (changed) {
                invalidate(s); s->had_session = true; s->saved_generation = auth_context_generation(active);
                snprintf(s->state.url, sizeof(s->state.url), "%s", auth_instance_url(active));
                s->state.provider = (unsigned)s->provider;
            }
            if (s->available && !s->state_failed) {
                char *snapshot = auth_session_export(active);
                if (snapshot && (!s->saved || strcmp(snapshot, s->saved)) && (!s->pending || strcmp(snapshot, s->pending))) {
                    /* Any update stays unavailable on restart until both the
                     * credential write and its public commit marker succeed. */
                    if (s->state.allowed) invalidate(s);
                    auth_session_free(&s->pending); s->pending = snapshot; snapshot = NULL;
                }
                auth_session_free(&snapshot);
            }
        } else if (s->had_session && (auth_get_state(active) == AUTH_IDLE || auth_session_rejected(active))) {
            invalidate(s); s->delete_wanted = true; s->had_session = false;
        } else if (auth_session_rejected(active) && s->written.allowed) {
            invalidate(s); s->delete_wanted = true;
        }
    }
    schedule_state(s);
#ifdef HAVE_GIO
    if (s->context) start_operation(s);
#endif
    return memcmp(&before, &s->state, sizeof(before)) || restoring != s->restore || available != s->available ||
           strcmp(before_notice, s->notice);
}
const char *session_store_last_url(const session_store *s) { return s ? s->state.url : ""; }
const char *session_store_notice(const session_store *s) { return s ? s->notice : ""; }
bool session_store_restoring(const session_store *s) { return s && s->restore; }
char *session_store_take_resume(session_store *s)
{ if (!s) return NULL; char *value = s->resume; s->resume = NULL; return value; }
void session_store_cancel_resume(session_store *s)
{
    if (!s) return;
#ifdef HAVE_GIO
    if (s->restore && s->cancel) { g_cancellable_cancel(s->cancel); unavailable(s); }
#endif
    s->restore = false; auth_session_free(&s->resume);
}
void session_store_unwatch(session_store *s, auth_client *client)
{
    if (!s || !client) return;
    auth_set_session_gate(client, NULL, NULL);
    if (s->active == client) { s->active = NULL; s->had_session = false; }
}
static void dispose(session_store *s)
{
#ifdef HAVE_GIO
    if (s->bus && s->wallet_signal) g_dbus_connection_signal_unsubscribe(s->bus, s->wallet_signal);
    g_clear_object(&s->bus); g_clear_object(&s->cancel);
#ifdef HAVE_LIBSECRET
    g_clear_object(&s->service); if (s->attributes) g_hash_table_unref(s->attributes);
#endif
    if (s->context) g_main_context_unref(s->context);
#endif
    auth_session_free(&s->resume); auth_session_free(&s->pending); auth_session_free(&s->saved);
    if (s->lock >= 0) close(s->lock);
    if (s->directory >= 0) close(s->directory);
    free(s);
}
#ifdef HAVE_GIO
static void release(session_store *s) { if (!--s->refs) dispose(s); }
#endif
void session_store_destroy(session_store *s)
{
    if (!s) return;
    if (s->active) auth_set_session_gate(s->active, NULL, NULL);
    s->active = NULL;
#ifdef HAVE_GIO
    if (s->context) {
        /* Drain durable updates without freezing an active TUI; login has
         * already ended here. An unfinished write leaves recovery disabled. */
        gint64 end = g_get_monotonic_time() + 2000000;
        while ((s->state_busy || s->dirty || s->operation_busy || s->pending) && g_get_monotonic_time() < end) {
            session_store_poll(s, NULL); g_usleep(1000);
        }
        s->dying = true; g_cancellable_cancel(s->cancel);
        if (s->bus && s->wallet_handle >= 0)
            g_dbus_connection_call(s->bus, wallet_name(s), wallet_path(s), "org.kde.KWallet", "close",
                                  g_variant_new("(ibs)", s->wallet_handle, FALSE, "papo-tui"), G_VARIANT_TYPE("(i)"),
                                  G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, NULL, NULL);
        if (s->bus && s->wallet_signal) { g_dbus_connection_signal_unsubscribe(s->bus, s->wallet_signal); s->wallet_signal = 0; }
        end = g_get_monotonic_time() + 500000;
        while (s->refs > 1 && g_get_monotonic_time() < end) { g_main_context_iteration(s->context, FALSE); g_usleep(1000); }
    }
    release(s);
#else
    schedule_state(s); dispose(s);
#endif
}
