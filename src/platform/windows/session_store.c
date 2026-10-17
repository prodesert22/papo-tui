#include "app/session_store.h"
#include "config/profiles.h"
#include "platform/windows/files.h"
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wincred.h>

enum operation { LOOKUP, INVALIDATE, SAVE, DELETE_CREDENTIAL };
struct session_store {
    char *path, *target, *pending, *saved, *resume;
    char url[2049], notice[192];
    HANDLE lock;
    GMainContext *context;
    auth_client *active;
    unsigned refs;
    uint64_t revision, committed, generation;
    bool restoring, available, had_session, busy, dirty, deleting, gate, failed, dying;
};
typedef struct {
    session_store *store;
    enum operation operation;
    uint64_t revision;
    char url[2049];
    char *snapshot;
    bool ok;
} session_job;
static void release(session_store *s)
{
    if (--s->refs)
        return;
    auth_session_free(&s->pending);
    auth_session_free(&s->saved);
    auth_session_free(&s->resume);
    if (s->lock != INVALID_HANDLE_VALUE)
        CloseHandle(s->lock);
    if (s->context)
        g_main_context_unref(s->context);
    free(s->path);
    free(s->target);
    free(s);
}
static bool marker(session_job *job, bool allowed)
{
    json_t *root = json_pack("{s:i,s:s,s:i,s:b,s:I}", "version", 1, "url", job->url, "provider", 4,
                             "restore_allowed", allowed, "revision", (json_int_t)job->revision);
    char *data = root ? json_dumps(root, JSON_COMPACT) : NULL;
    json_decref(root);
    bool ok = data && papo_win_write_private(job->store->path, data, strlen(data));
    free(data);
    return ok;
}
static void worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    (void)cancel;
    session_job *job = data;
    gunichar2 *target = g_utf8_to_utf16(job->store->target, -1, NULL, NULL, NULL);
    bool ok = target != NULL;
    if (ok && job->operation == LOOKUP) {
        PCREDENTIALW credential = NULL;
        ok = CredReadW((LPCWSTR)target, CRED_TYPE_GENERIC, 0, &credential);
        if (ok) {
            size_t n = credential->CredentialBlobSize;
            ok = n && n <= CRED_MAX_CREDENTIAL_BLOB_SIZE &&
                 !memchr(credential->CredentialBlob, 0, n);
            if (ok) {
                job->snapshot = malloc(n + 1);
                ok = job->snapshot != NULL;
                if (ok) {
                    memcpy(job->snapshot, credential->CredentialBlob, n);
                    job->snapshot[n] = 0;
                }
            }
            SecureZeroMemory(credential->CredentialBlob, n);
            CredFree(credential);
        }
    } else if (ok) {
        ok = marker(job, false);
        if (ok && job->operation == SAVE) {
            size_t n = strlen(job->snapshot);
            CREDENTIALW credential = {0};
            credential.Type = CRED_TYPE_GENERIC;
            credential.TargetName = (LPWSTR)target;
            credential.CredentialBlob = (LPBYTE)job->snapshot;
            credential.CredentialBlobSize = (DWORD)n;
            credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
            ok = n <= CRED_MAX_CREDENTIAL_BLOB_SIZE && CredWriteW(&credential, 0) &&
                 marker(job, true);
        } else if (ok && job->operation == DELETE_CREDENTIAL) {
            ok = CredDeleteW((LPCWSTR)target, CRED_TYPE_GENERIC, 0) ||
                 GetLastError() == ERROR_NOT_FOUND;
        }
    }
    g_free(target);
    job->ok = ok;
    g_task_return_boolean(task, ok);
}
static void job_free(gpointer data)
{
    session_job *job = data;
    auth_session_free(&job->snapshot);
    release(job->store);
    free(job);
}
static void done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    session_job *job = g_task_get_task_data(G_TASK(result));
    session_store *s = job->store;
    s->busy = false;
    if (s->dying)
        return;
    if (job->operation == LOOKUP) {
        if (s->restoring && job->ok && job->revision == s->revision) {
            s->resume = job->snapshot;
            job->snapshot = NULL;
        } else
            s->restoring = false;
    } else if (job->ok) {
        s->committed = job->revision;
        if (job->operation == SAVE && job->revision == s->revision) {
            auth_session_free(&s->saved);
            s->saved = job->snapshot;
            job->snapshot = NULL;
        }
    } else {
        s->failed = true;
        s->available = false;
        s->restoring = false;
        snprintf(s->notice, sizeof(s->notice),
                 "Não foi possível salvar a sessão no cofre. Entre novamente ao reabrir.");
    }
}
static void start(session_store *s, enum operation op)
{
    if (s->busy || s->dying)
        return;
    session_job *job = calloc(1, sizeof(*job));
    if (!job) {
        s->failed = true;
        s->available = s->restoring = false;
        snprintf(s->notice, sizeof(s->notice),
                 "Não foi possível preparar o cofre. Entre novamente ao reabrir.");
        return;
    }
    job->store = s;
    ++s->refs;
    job->operation = op;
    job->revision = s->revision;
    snprintf(job->url, sizeof(job->url), "%s", s->url);
    if (op == SAVE) {
        job->snapshot = s->pending;
        s->pending = NULL;
    }
    s->busy = true;
    g_main_context_push_thread_default(s->context);
    GTask *task = g_task_new(NULL, NULL, done, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, worker);
    g_object_unref(task);
    g_main_context_pop_thread_default(s->context);
}
static void invalidate(session_store *s)
{
    ++s->revision;
    s->dirty = true;
    auth_session_free(&s->pending);
    auth_session_free(&s->saved);
}
static int rotation_gate(void *context, auth_client *client)
{
    session_store *s = context;
    if (s->active != client)
        return 1;
    if (s->failed)
        return -1;
    if (!s->available)
        return 1;
    if (!s->gate) {
        invalidate(s);
        s->gate = true;
    }
    if (s->busy || s->dirty || s->committed != s->revision)
        return 0;
    s->gate = false;
    return 1;
}
session_store *session_store_create(const profile_config *config, bool restore)
{
    session_store *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->refs = 1;
    s->lock = INVALID_HANDLE_VALUE;
    s->context = g_main_context_new();
    char *directory = config && config->directory_path ? papo_win_directory(true) : NULL;
    if (!directory)
        goto disabled;
    char *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, config->directory_path, -1);
    char *name = g_strdup_printf("session-%s", scope);
    char *path = g_build_filename(directory, name, NULL);
    s->path = malloc(strlen(path) + 6);
    s->target = malloc(strlen(scope) + 9);
    if (s->path)
        sprintf(s->path, "%s.json", path);
    if (s->target)
        sprintf(s->target, "PapoTUI/%s", scope);
    char *lock = g_strconcat(path, ".lock", NULL);
    s->lock = papo_win_lock(lock);
    g_free(lock);
    g_free(path);
    g_free(name);
    g_free(scope);
    free(directory);
    if (!s->path || !s->target || s->lock == INVALID_HANDLE_VALUE)
        goto disabled;
    char *data = NULL;
    size_t length = 0;
    int loaded = papo_win_read_private(s->path, 8192, &data, &length);
    json_t *root = loaded == 1 ? json_loadb(data, length, JSON_REJECT_DUPLICATES, NULL) : NULL;
    free(data);
    if (loaded < 0 ||
        (loaded == 1 &&
         (!json_is_object(root) || json_integer_value(json_object_get(root, "version")) != 1 ||
          !json_is_string(json_object_get(root, "url")) ||
          !json_is_boolean(json_object_get(root, "restore_allowed"))))) {
        json_decref(root);
        goto disabled;
    }
    const char *url = json_string_value(json_object_get(root, "url"));
    if (url && strlen(url) < sizeof(s->url)) {
        char reason[192];
        char *canonical = auth_normalize_url(url, reason, sizeof(reason));
        if (canonical && !strcmp(canonical, url))
            snprintf(s->url, sizeof(s->url), "%s", url);
        curl_free(canonical);
    }
#ifdef PAPO_WINDOWS_CREDENTIALS
    s->available = true;
#endif
    s->restoring = s->available && restore && *s->url &&
                   json_is_true(json_object_get(root, "restore_allowed"));
    json_decref(root);
    if (s->restoring)
        start(s, LOOKUP);
    return s;
disabled:
    s->available = false;
    snprintf(s->notice, sizeof(s->notice),
             "Cofre indisponível ou em uso por outra janela. Sessão somente nesta execução.");
    return s;
}
bool session_store_poll(session_store *s, auth_client *active)
{
    if (!s)
        return false;
    bool changed = s->dirty || s->deleting;
    for (unsigned i = 0; i < 32 && g_main_context_pending(s->context); ++i) {
        g_main_context_iteration(s->context, FALSE);
        changed = true;
    }
    if (s->active != active && (!s->active || (active && auth_has_session(active)))) {
        s->active = active;
        s->had_session = false;
        if (active)
            auth_set_session_gate(active, rotation_gate, s);
    }
    active = s->active;
    if (active && auth_has_session(active) && !auth_busy(active)) {
        if (!s->had_session || strcmp(s->url, auth_instance_url(active)) ||
            s->generation != auth_context_generation(active)) {
            invalidate(s);
            s->had_session = true;
            s->generation = auth_context_generation(active);
            snprintf(s->url, sizeof(s->url), "%s", auth_instance_url(active));
        }
        if (s->available && !s->failed && !s->gate) {
            char *snapshot = auth_session_export(active);
            if (snapshot && (!s->saved || strcmp(snapshot, s->saved)) &&
                (!s->pending || strcmp(snapshot, s->pending)) && !s->busy) {
                auth_session_free(&s->pending);
                s->pending = snapshot;
                snapshot = NULL;
            }
            auth_session_free(&snapshot);
        }
    } else if (active && s->had_session &&
               (auth_get_state(active) == AUTH_IDLE || auth_session_rejected(active))) {
        invalidate(s);
        s->had_session = false;
        s->deleting = true;
    }
    if (!s->busy && s->path && s->lock != INVALID_HANDLE_VALUE && !s->failed) {
        if (s->dirty) {
            s->dirty = false;
            start(s, INVALIDATE);
        } else if (s->deleting) {
            s->deleting = false;
            start(s, DELETE_CREDENTIAL);
        } else if (s->pending && s->available)
            start(s, SAVE);
    }
    return changed;
}
void session_store_unwatch(session_store *s, auth_client *client)
{
    if (!s || !client)
        return;
    auth_set_session_gate(client, NULL, NULL);
    if (s->active == client)
        s->active = NULL;
}
const char *session_store_last_url(const session_store *s)
{
    return s ? s->url : "";
}
const char *session_store_notice(const session_store *s)
{
    return s ? s->notice : "";
}
bool session_store_restoring(const session_store *s)
{
    return s && s->restoring;
}
char *session_store_take_resume(session_store *s)
{
    if (!s)
        return NULL;
    char *resume = s->resume;
    s->resume = NULL;
    if (resume)
        s->restoring = false;
    return resume;
}
void session_store_cancel_resume(session_store *s)
{
    if (s) {
        s->restoring = false;
        auth_session_free(&s->resume);
    }
}
void session_store_destroy(session_store *s)
{
    if (!s)
        return;
    if (s->active)
        auth_set_session_gate(s->active, NULL, NULL);
    s->active = NULL;
    gint64 end = g_get_monotonic_time() + 2500000;
    while ((s->busy || s->dirty || s->deleting || s->pending) && !s->failed &&
           g_get_monotonic_time() < end) {
        session_store_poll(s, NULL);
        g_usleep(1000);
    }
    s->dying = true;
    release(s);
}
