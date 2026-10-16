#include "ui/media/media_process.h"
#include "config/app_mode.h"
#include "platform/runtime.h"
#include "ui/media/media_wire.h"
#include <gio/gio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    atomic_uint refs;
    atomic_bool cancelled;
    GMutex lock;
    GCond wake;
    uint64_t id, generation;
    gint64 deadline;
    GVariant *request, *response;
    bool done, cancel_sent;
} process_job;
typedef struct {
    GBytes *body;
    guint32 header;
    bool body_phase;
} outgoing;
static GAsyncQueue *requests;
static GMutex lifecycle;
static GSubprocess *child, *active;
static GCancellable *io_cancel;
static GMainContext *dispatcher_context;
static uint64_t connected_generation;
static atomic_uint_fast64_t generation = 1, next_id = 1;
static atomic_size_t released_bytes;
static gsize initialized;
static GHashTable *pending;
static GQueue outbound = G_QUEUE_INIT;
static bool reading, writing, read_body, io_failed;
static guint32 read_header;
static GBytes *incoming;
size_t media_process_released_bytes(void)
{
    return atomic_exchange(&released_bytes, 0);
}
uint64_t media_process_generation(void)
{
    return atomic_load(&generation);
}
static void job_unref(gpointer data)
{
    process_job *job = data;
    if (atomic_fetch_sub(&job->refs, 1) != 1)
        return;
    if (job->request) {
        size_t size = g_variant_get_size(job->request);
        g_variant_unref(job->request);
        atomic_fetch_add(&released_bytes, size);
    }
    if (job->response)
        g_variant_unref(job->response);
    g_cond_clear(&job->wake);
    g_mutex_clear(&job->lock);
    free(job);
}
static void finish(process_job *job, GVariant *response)
{
    g_mutex_lock(&job->lock);
    job->response = response;
    job->done = true;
    g_cond_signal(&job->wake);
    g_mutex_unlock(&job->lock);
}
static gpointer reap(gpointer data)
{
    GSubprocess *process = data;
    g_subprocess_wait(process, NULL, NULL);
    g_object_unref(process);
    return NULL;
}
static void retire_locked(void)
{
    if (!child)
        return;
    g_subprocess_force_exit(child);
    GThread *thread = g_thread_new("media-reap", reap, child);
    g_thread_unref(thread);
    child = NULL;
}
void media_process_stop(void)
{
    g_mutex_lock(&lifecycle);
    atomic_fetch_add(&generation, 1);
    if (io_cancel)
        g_cancellable_cancel(io_cancel);
    retire_locked();
    g_mutex_unlock(&lifecycle);
    GAsyncQueue *queue = g_atomic_pointer_get(&requests);
    if (queue)
        g_async_queue_push(queue, GINT_TO_POINTER(1));
}
static void outgoing_free(outgoing *message)
{
    g_bytes_unref(message->body);
    free(message);
}
static void disconnect_worker(void)
{
    g_mutex_lock(&lifecycle);
    if (io_cancel)
        g_cancellable_cancel(io_cancel);
    retire_locked();
    g_mutex_unlock(&lifecycle);
    /* Both streams complete cancellation on this private GIO context. The UI
     * never waits for pipe I/O, child teardown or the Windows pipe threads. */
    while (reading || writing)
        g_main_context_iteration(dispatcher_context, TRUE);
    g_mutex_lock(&lifecycle);
    g_clear_object(&io_cancel);
    g_mutex_unlock(&lifecycle);
    g_clear_object(&active);
    g_clear_pointer(&incoming, g_bytes_unref);
    read_body = false;
    io_failed = false;
    g_queue_clear_full(&outbound, (GDestroyNotify)outgoing_free);
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, pending);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        finish(value, NULL);
        g_hash_table_iter_remove(&iter);
    }
}
static bool connect_worker(uint64_t epoch)
{
    char *executable = papo_executable_path();
    char *directory = executable ? g_path_get_dirname(executable) : NULL;
    char *path = directory
                     ? g_build_filename(directory, "papo-backends", PAPO_MEDIA_WORKER_FILE, NULL)
                     : NULL;
    free(executable);
    if (!path || !g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) {
        g_free(path);
        path = g_build_filename(PAPO_BACKEND_INSTALL_DIR, PAPO_MEDIA_WORKER_FILE, NULL);
    }
    GError *error = NULL;
    g_mutex_lock(&lifecycle);
    if (epoch == atomic_load(&generation) && app_mode_get() != APP_MODE_ULTRALIGHT) {
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(
            G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_SILENCE);
#ifdef _WIN32
        /* The packaged DLLs live beside the TUI, one level above the helper.
         * Extend only the child's environment; never search PATH for helpers. */
        if (directory) {
            const char *inherited = g_getenv("PATH");
            char *search =
                g_strjoin(G_SEARCHPATH_SEPARATOR_S, directory, inherited ? inherited : "", NULL);
            g_subprocess_launcher_setenv(launcher, "PATH", search, TRUE);
            g_free(search);
        }
#endif
        active = g_subprocess_launcher_spawn(launcher, &error, path, NULL);
        g_object_unref(launcher);
    }
    if (active) {
        child = g_object_ref(active);
        io_cancel = g_cancellable_new();
        connected_generation = epoch;
    }
    g_mutex_unlock(&lifecycle);
    g_free(path);
    g_free(directory);
    g_clear_error(&error);
    return active != NULL;
}
static void queue_message(GVariant *value)
{
    outgoing *message = g_new0(outgoing, 1);
    g_variant_ref_sink(value);
    message->body = media_wire_store(value);
    g_variant_unref(value);
    message->header = GUINT32_TO_BE((guint32)g_bytes_get_size(message->body));
    g_queue_push_tail(&outbound, message);
}
static void read_complete(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void)unused;
    gsize size = 0;
    GError *error = NULL;
    bool ok = g_input_stream_read_all_finish(G_INPUT_STREAM(source), result, &size, &error);
    g_clear_error(&error);
    reading = false;
    if (!ok || size != (read_body ? g_bytes_get_size(incoming) : sizeof(read_header))) {
        io_failed = true;
        return;
    }
    if (!read_body) {
        size = GUINT32_FROM_BE(read_header);
        if (!size || size > MEDIA_WIRE_LIMIT) {
            io_failed = true;
            return;
        }
        void *buffer = NULL;
        incoming = media_wire_buffer(size, &buffer);
        read_body = true;
        reading = true;
        g_input_stream_read_all_async(G_INPUT_STREAM(source), buffer, size, G_PRIORITY_DEFAULT,
                                      io_cancel, read_complete, NULL);
        return;
    }
    GVariant *response =
        g_variant_ref_sink(g_variant_new_from_bytes(G_VARIANT_TYPE("(tbv)"), incoming, FALSE));
    atomic_fetch_add(&released_bytes, g_bytes_get_size(incoming));
    g_clear_pointer(&incoming, g_bytes_unref);
    read_body = false;
    if (!g_variant_is_normal_form(response)) {
        g_variant_unref(response);
        io_failed = true;
        return;
    }
    GVariant *id = g_variant_get_child_value(response, 0);
    uint64_t found = g_variant_get_uint64(id);
    g_variant_unref(id);
    process_job *job = g_hash_table_lookup(pending, &found);
    if (job) {
        finish(job, response);
        g_hash_table_remove(pending, &found);
    } else
        g_variant_unref(response);
}
static void write_complete(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void)unused;
    gsize size = 0;
    GError *error = NULL;
    bool ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), result, &size, &error);
    g_clear_error(&error);
    writing = false;
    outgoing *message = g_queue_peek_head(&outbound);
    if (!ok ||
        size != (message->body_phase ? g_bytes_get_size(message->body) : sizeof(message->header))) {
        io_failed = true;
        return;
    }
    if (message->body_phase) {
        outgoing_free(g_queue_pop_head(&outbound));
        return;
    }
    message->body_phase = true;
    writing = true;
    g_output_stream_write_all_async(G_OUTPUT_STREAM(source), g_bytes_get_data(message->body, NULL),
                                    g_bytes_get_size(message->body), G_PRIORITY_DEFAULT, io_cancel,
                                    write_complete, NULL);
}
static gpointer dispatch(gpointer unused)
{
    (void)unused;
    dispatcher_context = g_main_context_new();
    g_main_context_push_thread_default(dispatcher_context);
    pending = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, job_unref);
    process_job *waiting = NULL;
    for (;;) {
        while (g_main_context_iteration(dispatcher_context, FALSE)) {
        }
        if (active && (io_failed || connected_generation != atomic_load(&generation)))
            disconnect_worker();
        process_job *job = waiting ? waiting : g_async_queue_try_pop(requests);
        waiting = NULL;
        if (job == GINT_TO_POINTER(1))
            job = NULL;
        if (job) {
            if (job->generation != atomic_load(&generation) || atomic_load(&job->cancelled) ||
                app_mode_get() == APP_MODE_ULTRALIGHT ||
                (!active && !connect_worker(job->generation))) {
                finish(job, NULL);
                job_unref(job);
                continue;
            }
            uint64_t *key = g_new(uint64_t, 1);
            *key = job->id;
            g_hash_table_insert(pending, key, job);
            queue_message(job->request);
        }
        if (!active || (!g_hash_table_size(pending) && g_queue_is_empty(&outbound))) {
            if (active && app_mode_get() == APP_MODE_LIGHT) {
                waiting = g_async_queue_timeout_pop(requests, 3 * G_USEC_PER_SEC);
                if (!waiting && app_mode_get() == APP_MODE_LIGHT)
                    disconnect_worker();
            } else
                waiting = g_async_queue_pop(requests);
            continue;
        }
        bool expired = false;
        GHashTableIter iter;
        gpointer value;
        g_hash_table_iter_init(&iter, pending);
        while (g_hash_table_iter_next(&iter, NULL, &value)) {
            job = value;
            if (g_get_monotonic_time() > job->deadline) {
                expired = true;
                break;
            }
            if (atomic_load(&job->cancelled) && !job->cancel_sent) {
                job->cancel_sent = true;
                queue_message(g_variant_new("(uuttv)", MEDIA_WIRE_VERSION,
                                            (guint32)MEDIA_RPC_CANCEL, (guint64)job->generation,
                                            (guint64)job->id, g_variant_new_boolean(TRUE)));
            }
        }
        if (expired) {
            disconnect_worker();
            continue;
        }
        if (!reading && g_hash_table_size(pending)) {
            reading = true;
            g_input_stream_read_all_async(g_subprocess_get_stdout_pipe(active), &read_header,
                                          sizeof(read_header), G_PRIORITY_DEFAULT, io_cancel,
                                          read_complete, NULL);
        }
        if (!writing && !g_queue_is_empty(&outbound)) {
            outgoing *message = g_queue_peek_head(&outbound);
            writing = true;
            g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(active), &message->header,
                                            sizeof(message->header), G_PRIORITY_DEFAULT, io_cancel,
                                            write_complete, NULL);
        }
        g_usleep(1000);
    }
    return NULL;
}
GVariant *media_process_call(unsigned operation, GVariant *payload, bool (*cancelled)(void *),
                             void *context)
{
    if (g_once_init_enter(&initialized)) {
        g_atomic_pointer_set(&requests, g_async_queue_new());
        GThread *thread = g_thread_new("media-ipc", dispatch, NULL);
        g_thread_unref(thread);
        g_once_init_leave(&initialized, 1);
    }
    process_job *job = calloc(1, sizeof(*job));
    if (!job) {
        g_variant_ref_sink(payload);
        g_variant_unref(payload);
        return NULL;
    }
    atomic_init(&job->refs, 2);
    atomic_init(&job->cancelled, false);
    g_mutex_init(&job->lock);
    g_cond_init(&job->wake);
    job->id = atomic_fetch_add(&next_id, 1);
    job->generation = atomic_load(&generation);
    job->deadline = g_get_monotonic_time() + 30000000;
    job->request =
        g_variant_ref_sink(g_variant_new("(uuttv)", MEDIA_WIRE_VERSION, (guint32)operation,
                                         (guint64)job->generation, (guint64)job->id, payload));
    if (g_variant_get_size(job->request) > MEDIA_WIRE_LIMIT ||
        app_mode_get() == APP_MODE_ULTRALIGHT) {
        job_unref(job);
        job_unref(job);
        return NULL;
    }
    g_async_queue_push(requests, job);
    g_mutex_lock(&job->lock);
    while (!job->done) {
        gint64 now = g_get_monotonic_time();
        if (job->generation != atomic_load(&generation) || app_mode_get() == APP_MODE_ULTRALIGHT ||
            (cancelled && cancelled(context)) || now > job->deadline)
            atomic_store(&job->cancelled, true);
        /* Keep the cache reservation until IPC has drained the cancelled reply.
         * Otherwise late frame buffers escape both accounting and heap trimming. */
        if (now > job->deadline + 1000000)
            break;
        g_cond_wait_until(&job->wake, &job->lock, now + 30000);
    }
    GVariant *response = job->response;
    job->response = NULL;
    g_mutex_unlock(&job->lock);
    job_unref(job);
    return response;
}
