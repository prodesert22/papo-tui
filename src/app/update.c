#include "app/update.h"
#include "config/read_storage.h"
#include "model/version.h"
#include "net/clock.h"
#include "net/http/public_request.h"
#include "net/retry.h"
#include "net/update.h"
#include <glib.h>
#include <jansson.h>
#include <stdint.h>
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
} update;

/* {"version":1,"contexts":[]} or one {"checked":<unix seconds>,"latest":"<version>"}:
 * when the last successful check ran and the newest version it knew. */
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
    app_version version;
    return json_is_object(context) && json_object_size(context) == 2 && json_is_integer(checked) &&
           json_integer_value(checked) >= 0 && json_is_string(latest) &&
           json_string_length(latest) < APP_VERSION_TEXT &&
           strlen(json_string_value(latest)) == json_string_length(latest) &&
           app_version_parse(json_string_value(latest), &version);
}
static bool stored(uint64_t *checked, app_version *latest)
{
    read_storage *storage = read_storage_open_named(stem, validate);
    json_t *root = storage ? read_storage_load(storage) : NULL;
    read_storage_close(storage);
    json_t *context = json_array_get(json_object_get(root, "contexts"), 0);
    bool found = context != NULL;
    if (found) {
        *checked = (uint64_t)json_integer_value(json_object_get(context, "checked"));
        app_version_parse(json_string_value(json_object_get(context, "latest")), latest);
    }
    json_decref(root);
    return found;
}
static void store(uint64_t checked, const app_version *latest)
{
    char text[APP_VERSION_TEXT];
    if (!app_version_format(latest, text, sizeof(text)))
        return;
    read_storage *storage = read_storage_open_named(stem, validate);
    if (!storage)
        return;
    if (read_storage_lock(storage)) {
        json_t *root = json_pack("{s:i,s:[{s:I,s:s}]}", "version", 1, "contexts", "checked",
                                 (json_int_t)checked, "latest", text);
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
static void know(const app_version *latest)
{
    bool same = update.has_latest && !app_version_compare(latest, &update.latest);
    update.has_latest = app_version_format(latest, update.latest_text, sizeof(update.latest_text));
    update.latest = *latest;
    if (!same)
        update.acknowledged = false;
}
static void schedule(uint64_t now, uint64_t milliseconds)
{
    update.due = now + milliseconds;
    update.has_due = true;
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
    app_version latest;
    uint64_t wait = STARTUP_DELAY_MS;
    if (stored(&checked, &latest)) {
        if (app_version_compare(&latest, &update.current) > 0)
            know(&latest);
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
void update_shutdown(void)
{
    public_request_destroy(update.request);
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
    bool found = false;
    uint64_t wait = 0;
    if (public_request_state(update.request) == PUBLIC_READY) {
        long status = public_request_status(update.request);
        if (status == 200) {
            size_t length;
            const char *body = public_request_body(update.request, &length);
            update_parse parsed = update_parse_releases(body, length, update.list, &latest);
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
            know(&latest);
        else
            update.has_latest = false;
        update.state = newer ? UPDATE_AVAILABLE : UPDATE_CURRENT;
        update.failure = UPDATE_FAILURE_NONE;
        update.failures = 0;
        store(wall_seconds(), newer ? &latest : &update.current);
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
bool update_poll(void)
{
    uint64_t now;
    if (!update.ready || !net_now_ms(&now))
        return false;
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
