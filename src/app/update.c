#include "app/update.h"
#include "config/read_storage.h"
#include "model/version.h"
#include "net/clock.h"
#include "net/http/public_request.h"
#include "net/retry.h"
#include "net/signature.h"
#include "net/update.h"
#include "net/update_key.h"
#include "platform/file_transfer.h"
#include "platform/install.h"
#include "platform/runtime.h"
#include "platform/self_install.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Late enough for the first screen and the sign-in requests to go first. */
#define STARTUP_DELAY_MS UINT64_C(3000)
#define CHECK_INTERVAL_S UINT64_C(86400)
#define MANUAL_INTERVAL_MS UINT64_C(60000)
/* GitHub allows 60 anonymous requests an hour for an address. */
#define LIMITED_WAIT_S UINT64_C(3600)
#define AUTOMATIC_ATTEMPTS 3U

static const char *const stem = "update";

static struct {
    bool ready, automatic, development, list, manual, acknowledged;
    bool has_latest, has_due, has_started;
    app_version current, latest;
    char latest_text[APP_VERSION_TEXT];
    update_state state;
    update_failure failure;
    public_request *request;
    uint64_t due, started;
    unsigned failures;
    /* The file of the newer release for this installation, and its download:
     * a private temporary folder holds the file until it is checked and taken. */
    install_channel channel;
    update_asset asset;
    public_request *transfer;
    update_transfer transfer_state;
    update_transfer_failure transfer_failure;
    char *directory, *file, *saved;
    GTask *verify;
    GCancellable *cancel;
    bool discard;
    int percent;
    /* Replacing this copy: what is replaced, whether the download under way is
     * for that (directory is then a folder beside it), the tree unpacked from
     * the portable archive, and whether to apply it when the program ends. */
    char *target, *tree;
    bool install, unpacking, apply;
    /* The key releases must be signed with, and which of the two files that
     * prove it is being fetched: the signature first, then the manifest. */
    unsigned char *key;
    size_t key_length;
    bool manifest;
} update;
/* What the screen left to be done once it is gone. */
static struct {
    bool waiting;
    install_channel channel;
    char *file, *tree, *target, *staging;
    char version[APP_VERSION_TEXT];
} pending;

/* The asset as stored: {"name":...,"sha256":...,"size":...}. */
static bool asset_decode(const json_t *value, update_asset *asset)
{
    const json_t *name = json_object_get(value, "name"), *sum = json_object_get(value, "sha256");
    const json_t *size = json_object_get(value, "size");
    memset(asset, 0, sizeof(*asset));
    if (!json_is_object(value) || json_object_size(value) != 3 || !json_is_string(name) ||
        !json_is_string(sum) || !json_is_integer(size) || json_integer_value(size) <= 0 ||
        json_string_length(name) >= sizeof(asset->name) ||
        strlen(json_string_value(name)) != json_string_length(name) ||
        json_string_length(sum) != UPDATE_HASH_BYTES - 1 ||
        strlen(json_string_value(sum)) != json_string_length(sum))
        return false;
    snprintf(asset->name, sizeof(asset->name), "%s", json_string_value(name));
    snprintf(asset->sha256, sizeof(asset->sha256), "%s", json_string_value(sum));
    asset->size = (uint64_t)json_integer_value(size);
    return update_asset_valid(asset);
}
/* {"version":1,"contexts":[]} or one {"checked":<unix seconds>,"latest":"<version>"},
 * with "asset" when that release has a file for this installation: when the
 * last successful check ran and what it found. */
static bool validate(json_t *root)
{
    json_t *contexts = json_object_get(root, "contexts");
    if (!json_is_object(root) || json_object_size(root) != 2 ||
        !json_is_integer(json_object_get(root, "version")) ||
        json_integer_value(json_object_get(root, "version")) != 1 || !json_is_array(contexts) ||
        json_array_size(contexts) > 1)
        return false;
    if (!json_array_size(contexts))
        return true;
    json_t *context = json_array_get(contexts, 0);
    json_t *checked = json_object_get(context, "checked");
    json_t *latest = json_object_get(context, "latest");
    json_t *stored_asset = json_object_get(context, "asset");
    app_version version;
    update_asset asset;
    return json_is_object(context) && json_object_size(context) == (stored_asset ? 3U : 2U) &&
           json_is_integer(checked) && json_integer_value(checked) >= 0 && json_is_string(latest) &&
           json_string_length(latest) < APP_VERSION_TEXT &&
           strlen(json_string_value(latest)) == json_string_length(latest) &&
           app_version_parse(json_string_value(latest), &version) &&
           (!stored_asset || asset_decode(stored_asset, &asset));
}
static bool stored(uint64_t *checked, app_version *latest, update_asset *asset)
{
    read_storage *storage = read_storage_open_named(stem, validate);
    json_t *root = storage ? read_storage_load(storage) : NULL;
    read_storage_close(storage);
    json_t *context = json_array_get(json_object_get(root, "contexts"), 0);
    bool found = context != NULL;
    memset(asset, 0, sizeof(*asset));
    if (found) {
        *checked = (uint64_t)json_integer_value(json_object_get(context, "checked"));
        app_version_parse(json_string_value(json_object_get(context, "latest")), latest);
        if (json_object_get(context, "asset"))
            asset_decode(json_object_get(context, "asset"), asset);
    }
    json_decref(root);
    return found;
}
static void store(uint64_t checked, const app_version *latest, const update_asset *asset)
{
    char text[APP_VERSION_TEXT];
    if (!app_version_format(latest, text, sizeof(text)))
        return;
    read_storage *storage = read_storage_open_named(stem, validate);
    if (!storage)
        return;
    if (read_storage_lock(storage)) {
        json_t *context = json_pack("{s:I,s:s}", "checked", (json_int_t)checked, "latest", text);
        if (context && asset && *asset->name)
            json_object_set_new(context, "asset",
                                json_pack("{s:s,s:s,s:I}", "name", asset->name, "sha256",
                                          asset->sha256, "size", (json_int_t)asset->size));
        json_t *root = context ? json_pack("{s:i,s:[o]}", "version", 1, "contexts", context) : NULL;
        if (root)
            read_storage_write(storage, root);
        json_decref(root);
    }
    read_storage_close(storage);
}
static uint64_t wall_seconds(void)
{
    gint64 now = g_get_real_time();
    return now > 0 ? (uint64_t)now / G_USEC_PER_SEC : 0;
}
static void forget_saved(void)
{
    g_free(update.saved);
    update.saved = NULL;
}
static void know(const app_version *latest, const update_asset *asset)
{
    bool same = update.has_latest && !app_version_compare(latest, &update.latest);
    update.has_latest = app_version_format(latest, update.latest_text, sizeof(update.latest_text));
    update.latest = *latest;
    update.asset = *asset;
    if (!same) {
        update.acknowledged = false;
        /* A package saved earlier is of another version. */
        forget_saved();
    }
}
/* A test endpoint may also name the kind of installation to pretend; a
 * development build is otherwise no installation at all. */
static install_channel channel_of_run(bool development)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL"), *named = getenv("PAPO_UPDATE_CHANNEL");
    if (custom && *custom && named && *named)
        return install_channel_parse(named);
    return development ? INSTALL_UNKNOWN : install_detect();
}
static void schedule(uint64_t now, uint64_t milliseconds)
{
    update.due = now + milliseconds;
    update.has_due = true;
}
/* The key built into the program. A test endpoint may name another, as a DER
 * file, since nobody but the project can sign with the real one; a file that
 * cannot be read leaves no key, and then nothing is accepted. */
static void load_key(void)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL"), *named = getenv("PAPO_UPDATE_KEY");
    char *bytes = NULL;
    gsize length = 0;
    if (custom && *custom && named && *named) {
        if (g_file_get_contents(named, &bytes, &length, NULL) && length <= 4096) {
            update.key = (unsigned char *)bytes;
            update.key_length = length;
        } else
            g_free(bytes);
        return;
    }
    update.key = g_memdup2(update_release_key, sizeof(update_release_key));
    update.key_length = sizeof(update_release_key);
}
/* What a newer version replaces by itself, or NULL: an AppImage file or a
 * folder unpacked from the portable archive that this user can write. A test
 * endpoint that names the installation may name the place too. */
static char *target_of_run(void)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL"), *named = getenv("PAPO_UPDATE_CHANNEL");
    const char *place = getenv("PAPO_UPDATE_TARGET");
    if (custom && *custom && named && *named)
        return place && g_path_is_absolute(place) ? g_strdup(place) : NULL;
    return self_install_target(update.channel);
}
void update_init(bool automatic)
{
    update_shutdown();
    update.automatic = automatic;
    uint64_t now, checked = 0, wall = wall_seconds();
    if (!app_version_parse_build(PAPO_VERSION, &update.current, &update.development) ||
        !net_now_ms(&now))
        return;
    update.ready = true;
    load_key();
    update.channel = channel_of_run(update.development);
    update.target = target_of_run();
    /* The copy this one replaced was kept until the new one started. */
    if (update.channel == INSTALL_TARBALL && update.target)
        self_install_forget_previous(update.target);
    app_version latest;
    update_asset asset;
    uint64_t wait = STARTUP_DELAY_MS;
    if (stored(&checked, &latest, &asset)) {
        if (app_version_compare(&latest, &update.current) > 0) {
            /* The stored file counts only for the installation it was chosen for. */
            char text[APP_VERSION_TEXT], name[INSTALL_ASSET_BYTES];
            if (!*asset.name || !app_version_format(&latest, text, sizeof(text)) ||
                !install_asset_name(update.channel, install_architecture(), text, name,
                                    sizeof(name)) ||
                strcmp(name, asset.name))
                memset(&asset, 0, sizeof(asset));
            know(&latest, &asset);
        }
        /* A clock set back makes the stored time useless: check again. */
        if (checked <= wall && wall - checked < CHECK_INTERVAL_S) {
            update.state = update.has_latest ? UPDATE_AVAILABLE : UPDATE_CURRENT;
            uint64_t left = (CHECK_INTERVAL_S - (wall - checked)) * 1000;
            if (left > wait)
                wait = left;
        }
    }
    schedule(now, wait);
}
/* Removes the signature and the manifest fetched to check the release. */
static void forget_proof(void)
{
    static const char *const names[] = {UPDATE_MANIFEST_SIGNATURE, UPDATE_MANIFEST};
    for (size_t i = 0; i < 2; ++i) {
        char *path = g_build_filename(update.directory, names[i], NULL);
        g_unlink(path);
        g_free(path);
    }
}
/* Removes the temporary file and its folder, or the folder prepared beside
 * the installation with everything in it. */
static void discard_files(void)
{
    if (update.install)
        self_install_discard(update.directory);
    else {
        if (update.file)
            g_unlink(update.file);
        if (update.directory) {
            forget_proof();
            g_rmdir(update.directory);
        }
    }
    g_free(update.file);
    g_free(update.directory);
    g_free(update.tree);
    update.file = update.directory = update.tree = NULL;
    update.install = update.apply = false;
}
void update_shutdown(void)
{
    /* A newer version asked to be put in place outlives the screens. */
    if (update.apply && update.install && update.transfer_state == UPDATE_TRANSFER_READY &&
        !pending.waiting) {
        pending.waiting = true;
        pending.channel = update.channel;
        pending.file = update.file;
        pending.tree = update.tree;
        pending.target = update.target;
        pending.staging = update.directory;
        snprintf(pending.version, sizeof(pending.version), "%s", update.latest_text);
        update.file = update.tree = update.target = update.directory = NULL;
        update.install = false;
        update.transfer_state = UPDATE_TRANSFER_NONE;
    }
    update_download_cancel();
    /* A check of the file still running reads it: let it finish first. */
    while (update.verify && !g_task_get_completed(update.verify)) {
        file_transfer_tasks_poll();
        g_usleep(1000);
    }
    g_clear_object(&update.verify);
    g_clear_object(&update.cancel);
    discard_files();
    forget_saved();
    g_free(update.target);
    g_free(update.key);
    public_request_destroy(update.request);
    public_request_destroy(update.transfer);
    memset(&update, 0, sizeof(update));
}
void update_set_automatic(bool automatic)
{
    update.automatic = automatic;
}
bool update_suppressed(void)
{
    const char *value = getenv("PAPO_UPDATE_CHECK");
    return value && !strcmp(value, "0");
}
/* A test endpoint lets a development build exercise the schedule. */
static bool automatic_allowed(void)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL");
    return update.automatic && !update_suppressed() && (!update.development || (custom && *custom));
}
static void fail(update_failure failure)
{
    update.state = UPDATE_FAILED;
    update.failure = failure;
}
static void begin(uint64_t now, bool manual)
{
    char url[UPDATE_URL_BYTES];
    update.manual = manual;
    update.started = now;
    update.has_started = true;
    if (!update.request)
        update.request = public_request_create();
    if (!update.request || !update_request_url(&update.current, url, sizeof(url), &update.list) ||
        !public_request_get(update.request, url, update_request_headers,
                            update_request_header_count, UPDATE_RESPONSE_LIMIT)) {
        /* Nothing was sent: an invalid test endpoint or no memory. */
        fail(UPDATE_FAILURE_RESPONSE);
        schedule(now, CHECK_INTERVAL_S * 1000);
        return;
    }
    update.state = UPDATE_CHECKING;
    update.failure = UPDATE_FAILURE_NONE;
}
static void complete(uint64_t now)
{
    update_failure failure = UPDATE_FAILURE_NONE;
    app_version latest;
    update_asset asset = {0};
    bool found = false;
    uint64_t wait = 0;
    if (public_request_state(update.request) == PUBLIC_READY) {
        long status = public_request_status(update.request);
        if (status == 200) {
            size_t length;
            const char *body = public_request_body(update.request, &length);
            update_parse parsed = update_parse_releases(body, length, update.list, update.channel,
                                                        install_architecture(), &latest, &asset);
            found = parsed == UPDATE_PARSE_FOUND;
            if (parsed == UPDATE_PARSE_INVALID)
                failure = UPDATE_FAILURE_RESPONSE;
        } else if (status == 403 || status == 429) {
            failure = UPDATE_FAILURE_LIMITED;
            wait = public_request_retry_after(update.request);
        } else if (status != 404 || update.list)
            /* Without any final release, "latest" answers 404: nothing newer. */
            failure = UPDATE_FAILURE_RESPONSE;
    } else {
        public_error error = public_request_error(update.request);
        failure = error == PUBLIC_ERROR_TLS         ? UPDATE_FAILURE_TLS
                  : error == PUBLIC_ERROR_TOO_LARGE ? UPDATE_FAILURE_RESPONSE
                                                    : UPDATE_FAILURE_NETWORK;
    }
    public_request_cancel(update.request);
    if (failure == UPDATE_FAILURE_NONE) {
        bool newer = found && app_version_compare(&latest, &update.current) > 0;
        if (newer)
            know(&latest, &asset);
        else {
            update.has_latest = false;
            memset(&update.asset, 0, sizeof(update.asset));
        }
        update.state = newer ? UPDATE_AVAILABLE : UPDATE_CURRENT;
        update.failure = UPDATE_FAILURE_NONE;
        update.failures = 0;
        store(wall_seconds(), newer ? &latest : &update.current, newer ? &asset : NULL);
        schedule(now, CHECK_INTERVAL_S * 1000);
        return;
    }
    fail(failure);
    if (failure == UPDATE_FAILURE_LIMITED) {
        if (wait < LIMITED_WAIT_S)
            wait = LIMITED_WAIT_S;
        if (wait > CHECK_INTERVAL_S)
            wait = CHECK_INTERVAL_S;
        update.failures = 0;
        schedule(now, wait * 1000);
    } else if (update.manual) {
        /* A check on request never changes when the next automatic one runs,
         * unless that one was the check it replaced. */
        if (!update.has_due || update.due <= now)
            schedule(now, CHECK_INTERVAL_S * 1000);
    } else if (++update.failures < AUTOMATIC_ATTEMPTS)
        schedule(now, net_retry_ms(update.failures, g_random_int()));
    else {
        update.failures = 0;
        schedule(now, CHECK_INTERVAL_S * 1000);
    }
}
static bool check_poll(uint64_t now)
{
    if (update.state != UPDATE_CHECKING) {
        if (!update.has_due || now < update.due || !automatic_allowed())
            return false;
        begin(now, false);
        return true;
    }
    if (!public_request_poll(update.request))
        return false;
    complete(now);
    return true;
}
typedef struct {
    char *path;
    uint64_t size;
    char sha256[UPDATE_HASH_BYTES];
} verify_job;
static void verify_free(gpointer data)
{
    verify_job *job = data;
    g_free(job->path);
    g_free(job);
}
/* Worker thread: the file must have the published size and SHA-256. */
static void verify_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    verify_job *job = data;
    FILE *file = fopen(job->path, "rb");
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    unsigned char *buffer = g_malloc(64 * 1024);
    uint64_t bytes = 0;
    bool ok = file && sum;
    size_t n;
    while (ok && (n = fread(buffer, 1, 64 * 1024, file))) {
        bytes += n;
        if (bytes > job->size || g_cancellable_is_cancelled(cancel))
            ok = false;
        else
            g_checksum_update(sum, buffer, (gssize)n);
    }
    ok = ok && !ferror(file) && bytes == job->size &&
         !strcmp(g_checksum_get_string(sum), job->sha256);
    if (file)
        fclose(file);
    if (sum)
        g_checksum_free(sum);
    g_free(buffer);
    g_task_return_boolean(task, ok);
}
static void transfer_fail(update_transfer_failure failure)
{
    discard_files();
    update.transfer_state = UPDATE_TRANSFER_FAILED;
    update.transfer_failure = failure;
}
typedef struct {
    char *archive, *staging;
    char version[APP_VERSION_TEXT];
} unpack_job;
static void unpack_free(gpointer data)
{
    unpack_job *job = data;
    g_free(job->archive);
    g_free(job->staging);
    g_free(job);
}
/* Worker thread: the checked archive becomes the tree of the newer version. */
static void unpack_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    unpack_job *job = data;
    g_task_return_pointer(
        task, self_install_unpack(job->archive, job->staging, job->version, cancel), g_free);
}
static void unpack_begin(void)
{
    unpack_job *job = g_new0(unpack_job, 1);
    job->archive = g_strdup(update.file);
    job->staging = g_strdup(update.directory);
    snprintf(job->version, sizeof(job->version), "%s", update.latest_text);
    update.cancel = g_cancellable_new();
    update.verify = file_transfer_task_new(update.cancel);
    update.unpacking = true;
    g_task_set_task_data(update.verify, job, unpack_free);
    g_task_run_in_thread(update.verify, unpack_worker);
    update.transfer_state = UPDATE_TRANSFER_UNPACKING;
}
static void verify_begin(void)
{
    verify_job *job = g_new0(verify_job, 1);
    job->path = g_strdup(update.file);
    job->size = update.asset.size;
    snprintf(job->sha256, sizeof(job->sha256), "%s", update.asset.sha256);
    update.cancel = g_cancellable_new();
    update.verify = file_transfer_task_new(update.cancel);
    g_task_set_task_data(update.verify, job, verify_free);
    g_task_run_in_thread(update.verify, verify_worker);
    update.transfer_state = UPDATE_TRANSFER_VERIFYING;
}
/* Starts fetching one of the release's two proof files into the folder. */
static bool proof_fetch(const char *name, size_t limit)
{
    char url[UPDATE_URL_BYTES];
    char *path = g_build_filename(update.directory, name, NULL);
    bool started =
        update_release_url(&update.latest, name, url, sizeof(url)) &&
        public_request_download(update.transfer, url, path, limit, update_redirect_allowed, NULL);
    g_free(path);
    return started;
}
/* The manifest is the release's when the project's key signed it, and the
 * package is the release's when the manifest lists it with the sum GitHub
 * reported: only then is the package worth fetching. */
static bool authentic(void)
{
    char *signature_path = g_build_filename(update.directory, UPDATE_MANIFEST_SIGNATURE, NULL);
    char *manifest_path = g_build_filename(update.directory, UPDATE_MANIFEST, NULL);
    char *signature = NULL, *manifest = NULL, sum[UPDATE_HASH_BYTES];
    gsize signature_length = 0, manifest_length = 0;
    bool ok =
        update.key && g_file_get_contents(signature_path, &signature, &signature_length, NULL) &&
        g_file_get_contents(manifest_path, &manifest, &manifest_length, NULL) &&
        signature_verify((const unsigned char *)manifest, manifest_length, signature,
                         signature_length, update.key, update.key_length) == SIGNATURE_VALID &&
        update_manifest_hash(manifest, manifest_length, update.asset.name, sum) &&
        !strcmp(sum, update.asset.sha256);
    g_free(manifest);
    g_free(signature);
    g_free(manifest_path);
    g_free(signature_path);
    return ok;
}
static bool asset_fetch(void)
{
    char url[UPDATE_URL_BYTES];
    return update_asset_url(&update.latest, &update.asset, url, sizeof(url)) &&
           public_request_download(update.transfer, url, update.file, (size_t)update.asset.size,
                                   update_redirect_allowed, NULL);
}
static update_transfer_failure transport_failure(public_error error)
{
    return error == PUBLIC_ERROR_TLS         ? UPDATE_TRANSFER_TLS
           : error == PUBLIC_ERROR_TOO_LARGE ? UPDATE_TRANSFER_MISMATCH
           : error == PUBLIC_ERROR_REDIRECT  ? UPDATE_TRANSFER_REFUSED
           : error == PUBLIC_ERROR_STORAGE   ? UPDATE_TRANSFER_STORAGE
                                             : UPDATE_TRANSFER_NETWORK;
}
/* The signature, then the manifest, then the check of both. */
static bool authenticate_poll(void)
{
    if (!public_request_poll(update.transfer))
        return false;
    public_state state = public_request_state(update.transfer);
    if (state == PUBLIC_FAILED) {
        public_error error = public_request_error(update.transfer);
        public_request_cancel(update.transfer);
        /* A proof larger than any real one is no proof. */
        transfer_fail(error == PUBLIC_ERROR_TOO_LARGE ? UPDATE_TRANSFER_FORGED
                                                      : transport_failure(error));
        return true;
    }
    if (state != PUBLIC_READY)
        return true;
    long status = public_request_status(update.transfer);
    public_request_cancel(update.transfer);
    if (status != 200) {
        /* A release without these files was not signed for this program. */
        transfer_fail(status == 404 ? UPDATE_TRANSFER_UNSIGNED : UPDATE_TRANSFER_REFUSED);
        return true;
    }
    if (!update.manifest) {
        update.manifest = true;
        if (!proof_fetch(UPDATE_MANIFEST, UPDATE_MANIFEST_LIMIT))
            transfer_fail(UPDATE_TRANSFER_STORAGE);
        return true;
    }
    if (!authentic()) {
        transfer_fail(UPDATE_TRANSFER_FORGED);
        return true;
    }
    forget_proof();
    if (!asset_fetch()) {
        transfer_fail(UPDATE_TRANSFER_STORAGE);
        return true;
    }
    update.transfer_state = UPDATE_TRANSFER_RUNNING;
    return true;
}
static bool download_poll(void)
{
    bool changed = false;
    if (update.transfer_state == UPDATE_TRANSFER_AUTHENTICATING)
        changed = authenticate_poll();
    else if (update.transfer_state == UPDATE_TRANSFER_RUNNING) {
        changed = public_request_poll(update.transfer);
        public_state state = public_request_state(update.transfer);
        if (state == PUBLIC_READY) {
            long status = public_request_status(update.transfer);
            public_request_cancel(update.transfer);
            if (status == 200)
                verify_begin();
            else
                transfer_fail(UPDATE_TRANSFER_REFUSED);
            changed = true;
        } else if (state == PUBLIC_FAILED) {
            public_error error = public_request_error(update.transfer);
            public_request_cancel(update.transfer);
            transfer_fail(transport_failure(error));
            changed = true;
        } else {
            int percent = update_download_percent();
            changed |= percent != update.percent;
            update.percent = percent;
        }
    }
    if (update.verify) {
        file_transfer_tasks_poll();
        if (g_task_get_completed(update.verify)) {
            bool unpacked = update.unpacking;
            char *tree = unpacked ? g_task_propagate_pointer(update.verify, NULL) : NULL;
            bool ok = unpacked ? tree != NULL : g_task_propagate_boolean(update.verify, NULL);
            g_clear_object(&update.verify);
            g_clear_object(&update.cancel);
            update.unpacking = false;
            if (update.discard) {
                update.discard = false;
                g_free(tree);
                discard_files();
            } else if (!ok)
                transfer_fail(unpacked ? UPDATE_TRANSFER_UNUSABLE : UPDATE_TRANSFER_MISMATCH);
            else if (unpacked) {
                update.tree = tree;
                update.transfer_state = UPDATE_TRANSFER_READY;
            } else if (update.install && update.channel == INSTALL_TARBALL)
                /* The archive is the published one: now it must hold the program. */
                unpack_begin();
            else
                update.transfer_state = UPDATE_TRANSFER_READY;
            changed = true;
        }
    }
    return changed;
}
bool update_poll(void)
{
    uint64_t now;
    if (!update.ready || !net_now_ms(&now))
        return false;
    bool changed = check_poll(now);
    changed |= download_poll();
    return changed;
}
bool update_check_now(void)
{
    uint64_t now;
    if (!update.ready || update.state == UPDATE_CHECKING || !net_now_ms(&now) ||
        (update.has_started && now - update.started < MANUAL_INTERVAL_MS))
        return false;
    begin(now, true);
    return true;
}
update_state update_status(void)
{
    return update.state;
}
update_failure update_error(void)
{
    return update.state == UPDATE_FAILED ? update.failure : UPDATE_FAILURE_NONE;
}
const char *update_latest(void)
{
    return update.has_latest ? update.latest_text : "";
}
bool update_development(void)
{
    return update.development;
}
bool update_notice(void)
{
    return update.has_latest && !update.acknowledged;
}
void update_acknowledge(void)
{
    update.acknowledged = true;
}
install_channel update_channel(void)
{
    return update.channel;
}
const char *update_asset_name(void)
{
    return update.has_latest ? update.asset.name : "";
}
uint64_t update_asset_size(void)
{
    return update.has_latest ? update.asset.size : 0;
}
/* Downloads the package into a temporary folder, or, to replace this copy,
 * into a folder beside it. */
static bool download_start(bool install)
{
    if (!update.ready || !*update_asset_name() || (install && !update.target) ||
        update.transfer_state == UPDATE_TRANSFER_AUTHENTICATING ||
        update.transfer_state == UPDATE_TRANSFER_RUNNING ||
        update.transfer_state == UPDATE_TRANSFER_VERIFYING ||
        update.transfer_state == UPDATE_TRANSFER_UNPACKING || update.verify)
        return false;
    discard_files();
    update.percent = 0;
    update.transfer_failure = UPDATE_TRANSFER_OK;
    update.install = install;
    if (install)
        update.directory = self_install_staging(update.target);
    else {
        update.directory = g_build_filename(g_get_tmp_dir(), "papo-update-XXXXXX", NULL);
        if (!papo_mkdtemp(update.directory)) {
            g_free(update.directory);
            update.directory = NULL;
        }
    }
    if (!update.directory) {
        transfer_fail(UPDATE_TRANSFER_STORAGE);
        return true;
    }
    update.file = g_build_filename(update.directory, update.asset.name, NULL);
    if (!update.transfer)
        update.transfer = public_request_create();
    /* Nothing of the package is fetched before the release proves who made it. */
    update.manifest = false;
    if (!update.transfer || !proof_fetch(UPDATE_MANIFEST_SIGNATURE, SIGNATURE_ARMOR_LIMIT)) {
        transfer_fail(UPDATE_TRANSFER_STORAGE);
        return true;
    }
    update.transfer_state = UPDATE_TRANSFER_AUTHENTICATING;
    return true;
}
bool update_download_start(void)
{
    return download_start(false);
}
bool update_install_possible(void)
{
    return update.target != NULL && *update_asset_name() &&
           (update.channel == INSTALL_APPIMAGE || update.channel == INSTALL_TARBALL);
}
bool update_install_start(void)
{
    return update_install_possible() && download_start(true);
}
bool update_install_staged(void)
{
    return update.install && update.transfer_state == UPDATE_TRANSFER_READY;
}
bool update_install_on_exit(void)
{
    if (!update_install_staged())
        return false;
    update.apply = true;
    return true;
}
bool update_apply(update_applied *result)
{
    if (!pending.waiting)
        return false;
    pending.waiting = false;
    memset(result, 0, sizeof(*result));
    snprintf(result->version, sizeof(result->version), "%s", pending.version);
    result->done = pending.channel == INSTALL_TARBALL
                       ? self_install_replace_tree(pending.tree, pending.target)
                       : self_install_replace_file(pending.file, pending.target);
    /* What could not be put in place stays where it is, for the user. */
    const char *kept = pending.channel == INSTALL_TARBALL ? pending.tree : pending.file;
    snprintf(result->place, sizeof(result->place), "%s", result->done ? pending.target : kept);
    if (result->done)
        self_install_discard(pending.staging);
    g_free(pending.file);
    g_free(pending.tree);
    g_free(pending.target);
    g_free(pending.staging);
    memset(&pending, 0, sizeof(pending));
    return true;
}
void update_download_cancel(void)
{
    if (update.transfer_state == UPDATE_TRANSFER_RUNNING ||
        update.transfer_state == UPDATE_TRANSFER_AUTHENTICATING)
        public_request_cancel(update.transfer);
    if (update.verify) {
        /* The worker still reads the file: it is removed when it returns. */
        g_cancellable_cancel(update.cancel);
        update.discard = true;
    } else
        discard_files();
    update.transfer_state = UPDATE_TRANSFER_NONE;
    update.transfer_failure = UPDATE_TRANSFER_OK;
}
update_transfer update_download_status(void)
{
    return update.transfer_state;
}
update_transfer_failure update_download_error(void)
{
    return update.transfer_state == UPDATE_TRANSFER_FAILED ? update.transfer_failure
                                                           : UPDATE_TRANSFER_OK;
}
int update_download_percent(void)
{
    if (update.transfer_state == UPDATE_TRANSFER_AUTHENTICATING)
        return 0;
    if (update.transfer_state != UPDATE_TRANSFER_RUNNING || !update.asset.size)
        return update.transfer_state == UPDATE_TRANSFER_NONE ? 0 : 100;
    uint64_t bytes = public_request_bytes(update.transfer);
    return bytes >= update.asset.size ? 100 : (int)(bytes * 100 / update.asset.size);
}
char *update_download_take(void)
{
    if (update.transfer_state != UPDATE_TRANSFER_READY || update.install)
        return NULL;
    char *file = update.file;
    update.file = NULL;
    update.transfer_state = UPDATE_TRANSFER_NONE;
    return file;
}
void update_download_finish(const char *saved)
{
    discard_files();
    forget_saved();
    if (saved && *saved)
        update.saved = g_strdup(saved);
}
const char *update_saved(void)
{
    return update.saved ? update.saved : "";
}
