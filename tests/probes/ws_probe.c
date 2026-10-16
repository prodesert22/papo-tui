#define _POSIX_C_SOURCE 200809L
#include "net/auth/client.h"
#include "net/clock.h"
#include "net/rate_limit.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void pause_tick(void)
{
    const struct timespec delay = {.tv_nsec = 10000000};
    nanosleep(&delay, NULL);
}

static bool finish(auth_client *client)
{
    uint64_t start, now;
    if (!net_now_ms(&start))
        return false;
    do {
        auth_poll(client);
        if (!auth_busy(client))
            return true;
        pause_tick();
    } while (net_now_ms(&now) && now - start < 20000);
    return false;
}

static bool fragment_valid(const json_t *event)
{
    const char *content = json_string_value(json_object_get(event, "content"));
    if (!content || strlen(content) != 16384)
        return false;
    for (size_t i = 0; i < 16384; i += 2)
        if ((unsigned char)content[i] != 0xc3 || (unsigned char)content[i + 1] != 0xa1)
            return false;
    return true;
}

/* Standalone transport mode uses only synthetic fixture cookies. */
static int transport(const char *base)
{
    char error[192];
    char *normalized = auth_normalize_url(base, error, sizeof(error));
    if (!normalized)
        return 2;
    CURLSH *share = curl_share_init();
    CURL *cookie = curl_easy_init();
    ws_client *ws = NULL;
    int result = 2;
    if (!share || !cookie ||
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK)
        goto done;
    curl_easy_setopt(cookie, CURLOPT_SHARE, share);
    curl_easy_setopt(cookie, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(cookie, CURLOPT_COOKIELIST,
                     "127.0.0.1\tFALSE\t/\tFALSE\t0\tAuth\tuser-fixture");
    curl_easy_setopt(cookie, CURLOPT_COOKIELIST,
                     "localhost\tFALSE\t/\tFALSE\t0\tAuth\tuser-fixture");
    ws = ws_create(share);
    if (!ws || !ws_connect(ws, normalized))
        goto done;
    uint64_t start, now;
    if (!net_now_ms(&start))
        goto done;
    bool ack = false;
    do {
        ws_poll(ws);
        json_t *event;
        while ((event = ws_pop(ws))) {
            const char *type = json_string_value(json_object_get(event, "type"));
            if (type && strcmp(type, "heartbeat_ack") == 0)
                ack = true;
            json_decref(event);
        }
        if (ack || ws_get_state(ws) == WS_ERROR)
            break;
        pause_tick();
    } while (net_now_ms(&now) && now - start < 8000);
    printf("ws_open=%d\nws_error=%d\nack=%d\nws_status=%ld\n%s\n", ws_get_state(ws) == WS_OPEN,
           ws_get_state(ws) == WS_ERROR, ack, ws_http_status(ws), ws_message(ws));
    result = ack ? 0 : 2;
done:
    ws_destroy(ws);
    if (cookie)
        curl_easy_cleanup(cookie);
    if (share)
        curl_share_cleanup(share);
    curl_free(normalized);
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    const char *mode = argc > 2 ? argv[2] : "";
    if (strcmp(mode, "transport") == 0) {
        int result = transport(argv[1]);
        curl_global_cleanup();
        return result;
    }
    auth_client *client = auth_create();
    if (!client) {
        curl_global_cleanup();
        return 1;
    }
    const char *user = getenv("PAPO_PROBE_USERNAME"), *password = getenv("PAPO_PROBE_PASSWORD");
    const char *private_password = getenv("PAPO_PROBE_SERVER_PASSWORD");
    auth_start(client, argv[1], user ? user : "josé", password ? password : "Synthetic!123",
               private_password ? private_password : "");
    if (!finish(client) || auth_get_state(client) != AUTH_READY) {
        printf("rest_ready=0\n%s\n", auth_message(client));
        auth_destroy(client);
        curl_global_cleanup();
        return 2;
    }
    puts("rest_ready=1");
    if (strcmp(mode, "queued-cancel") == 0)
        while (net_rate_try_acquire() == 1) {
        }
    auth_connect_ws(client);
    if (strcmp(mode, "cancel") == 0 || strcmp(mode, "queued-cancel") == 0) {
        auth_poll(client);
        printf("queued=%d\n", auth_ws_state(client) == WS_QUEUED);
        auth_cancel(client);
        printf("cancelled=%d\n",
               auth_ws_state(client) == WS_IDLE && auth_get_state(client) == AUTH_IDLE);
        auth_destroy(client);
        curl_global_cleanup();
        return 0;
    }
    uint64_t start, now;
    if (!net_now_ms(&start)) {
        auth_destroy(client);
        curl_global_cleanup();
        return 1;
    }
    bool ack = false, presence = false, fragment = false, filled = false;
    unsigned events = 0;
    unsigned timeout = strcmp(mode, "revalidate") == 0 ? 35000 : 14000;
    do {
        auth_poll(client);
        if (strcmp(mode, "queue") != 0) {
            json_t *event;
            while ((event = auth_next_event(client))) {
                const char *type = json_string_value(json_object_get(event, "type"));
                ++events;
                if (type && strcmp(type, "heartbeat_ack") == 0)
                    ack = true;
                if (type && strcmp(type, "presence_sync") == 0)
                    presence = true;
                if (type && strcmp(type, "message") == 0)
                    fragment = fragment_valid(event);
                json_decref(event);
            }
        }
        if (!filled && strcmp(mode, "send-queue") == 0 && auth_ws_state(client) == WS_OPEN) {
            json_t *event = json_pack("{s:s}", "type", "presence_activity");
            if (event)
                for (size_t i = 0; i <= WS_QUEUE_LIMIT; ++i)
                    auth_send_event(client, event);
            json_decref(event);
            filled = true;
        }
        if (auth_ws_state(client) == WS_ERROR || auth_ws_state(client) == WS_CLOSED ||
            auth_get_state(client) == AUTH_ERROR)
            break;
        if (ack && (strcmp(mode, "") == 0 || (strcmp(mode, "fragments") == 0 && fragment)))
            break;
        pause_tick();
    } while (net_now_ms(&now) && now - start < timeout);
    bool open = auth_ws_state(client) == WS_OPEN;
    printf("ws_open=%d\nws_error=%d\nws_closed=%d\nws_status=%ld\nack=%d\npresence=%d\nfragment=%"
           "d\nevents=%u\nrevalidated_failure=%d\n%s\n",
           open, auth_ws_state(client) == WS_ERROR, auth_ws_state(client) == WS_CLOSED,
           auth_ws_http_status(client), ack, presence, fragment, events,
           auth_get_state(client) == AUTH_ERROR, auth_ws_message(client));
    if (auth_get_state(client) == AUTH_READY) {
        auth_logout(client);
        bool ended = finish(client);
        printf("logout=%d\n",
               ended && auth_get_state(client) == AUTH_IDLE && auth_ws_state(client) != WS_OPEN);
    }
    auth_destroy(client);
    curl_global_cleanup();
    return strcmp(mode, "") != 0 || (open && ack) ? 0 : 2;
}
