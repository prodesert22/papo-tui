#define _POSIX_C_SOURCE 200809L
#include "config/profiles.h"
#include "net/auth/client.h"
#include "net/rate_limit.h"

#include <curl/curl.h>
#include <jansson.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>

static void pause_poll(auth_client *client)
{
    const struct timespec interval = {.tv_nsec = 1000000};
    auth_poll(client);
    nanosleep(&interval, NULL);
}

static void finish(auth_client *client)
{
    while (auth_busy(client))
        pause_poll(client);
}

static int emit(json_t *result)
{
    char *encoded = result ? json_dumps(result, JSON_COMPACT | JSON_SORT_KEYS) : NULL;
    json_decref(result);
    if (!encoded)
        return 1;
    puts(encoded);
    free(encoded);
    return 0;
}

static int discovery(int argc, char **argv)
{
    const char *mode = argv[1];
    if (argc < 3)
        return 1;
    auth_client *client = auth_create();
    if (!client)
        return 1;
    bool old_ready = false, old_ws_open = false;
    bool allow_http = argc > 3 && strcmp(argv[3], "allow") == 0;
    if (strcmp(mode, "rate-discovery") == 0) {
        if (!net_rate_configure(2, 1)) {
            auth_destroy(client);
            return 1;
        }
        allow_http = true;
    }
    if (strcmp(mode, "switch") == 0 || strcmp(mode, "pending") == 0) {
        if (argc != 4) {
            auth_destroy(client);
            return 1;
        }
        if (strcmp(mode, "switch") == 0) {
            auth_start(client, argv[2], "josé", "Synthetic!123", "");
            finish(client);
            old_ready = auth_get_state(client) == AUTH_READY;
            if (old_ready) {
                auth_connect_ws(client);
                for (int i = 0; i < 2000 && auth_ws_state(client) != WS_OPEN; ++i)
                    pause_poll(client);
                old_ws_open = auth_ws_state(client) == WS_OPEN;
            }
        } else {
            auth_discover(client, argv[2], true);
            for (int i = 0; i < 100; ++i)
                pause_poll(client);
        }
        auth_destroy(client);
        client = auth_create();
        if (!client)
            return 1;
        auth_discover(client, argv[3], true);
        finish(client);
        for (int i = 0; i < 700; ++i)
            pause_poll(client);
    } else if (strcmp(mode, "queued-cancel") == 0) {
        while (net_rate_try_acquire() == 1) {
        }
        auth_discover(client, argv[2], true);
        auth_poll(client);
        auth_cancel(client);
    } else {
        auth_discover(client, argv[2], allow_http);
        finish(client);
        if (strcmp(mode, "rate-discovery") == 0) {
            for (int i = 0; i < 2; ++i) {
                auth_destroy(client);
                client = auth_create();
                if (!client)
                    return 1;
                auth_discover(client, argv[2], true);
                finish(client);
            }
        }
    }
    json_t *result = json_pack(
        "{s:b,s:b,s:b,s:b,s:b,s:s,s:s}", "identified", auth_get_state(client) == AUTH_IDENTIFIED,
        "cancelled", auth_get_state(client) == AUTH_IDLE, "public", auth_server_public(client),
        "old_ready", old_ready, "old_ws_open", old_ws_open, "name", auth_server_name(client),
        "message", auth_message(client));
    auth_destroy(client);
    return emit(result);
}

int main(int argc, char **argv)
{
    if (argc < 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    const char *mode = argv[1];
    if (strcmp(mode, "discover") == 0 || strcmp(mode, "switch") == 0 ||
        strcmp(mode, "pending") == 0 || strcmp(mode, "queued-cancel") == 0 ||
        strcmp(mode, "rate-discovery") == 0) {
        int result = discovery(argc, argv);
        curl_global_cleanup();
        return result;
    }
    profile_config *config = profiles_create();
    if (!config) {
        curl_global_cleanup();
        return 1;
    }
    bool ok = true;
    char id[PROFILE_ID_BYTES] = "";
    if (strcmp(mode, "add") == 0 && argc == 5)
        ok = profiles_update(config, "", argv[2], argv[3], strcmp(argv[4], "allow") == 0, id);
    else if (strcmp(mode, "edit") == 0 && argc == 6)
        ok = profiles_update(config, argv[2], argv[3], argv[4], strcmp(argv[5], "allow") == 0, id);
    else if (strcmp(mode, "remove") == 0 && argc == 3)
        ok = profiles_remove(config, argv[2]);
    else if (strcmp(mode, "select") == 0 && argc == 4)
        ok = profiles_select(config, argv[2], strcmp(argv[3], "allow") == 0);
    else if (strcmp(mode, "notifications") == 0 && argc == 4)
        ok = profiles_set_notifications(config, strcmp(argv[2], "true") == 0,
                                        strcmp(argv[3], "true") == 0);
    else if (strcmp(mode, "rate") == 0 && argc == 4) {
        json_error_t error;
        json_t *rate = json_loads(argv[2], JSON_DECODE_ANY, &error);
        json_t *burst = json_loads(argv[3], JSON_DECODE_ANY, &error);
        ok = json_is_number(rate) && json_is_integer(burst) && json_integer_value(burst) > 0 &&
             json_integer_value(burst) <= UINT_MAX;
        if (ok)
            ok = profiles_set_rate(config, json_number_value(rate),
                                   (unsigned)json_integer_value(burst));
        json_decref(rate);
        json_decref(burst);
    } else if (strcmp(mode, "stale") == 0 || strcmp(mode, "locked") == 0) {
        profile_config *other = profiles_create();
        if (!other) {
            profiles_destroy(config);
            curl_global_cleanup();
            return 1;
        }
        if (strcmp(mode, "stale") == 0) {
            ok = profiles_update(other, "", "Other writer", "https://example.com/other", false, id);
            if (ok)
                ok = profiles_update(config, "", "Stale writer", "https://example.com/stale", false,
                                     id);
        } else {
            if (flock(other->directory, LOCK_EX | LOCK_NB) < 0) {
                profiles_destroy(other);
                profiles_destroy(config);
                curl_global_cleanup();
                return 1;
            }
            ok = profiles_update(config, "", "Locked writer", "https://example.com/locked", false,
                                 id);
            flock(other->directory, LOCK_UN);
        }
        profiles_destroy(other);
    } else if (strcmp(mode, "list") != 0)
        ok = false;
    json_t *items = json_array();
    for (size_t i = 0; items && i < config->count; ++i) {
        const server_profile *profile = &config->profiles[i];
        if (json_array_append_new(items, json_pack("{s:s,s:s,s:s}", "id", profile->id, "name",
                                                   profile->name, "url", profile->url)) < 0) {
            json_decref(items);
            items = NULL;
        }
    }
    json_t *result =
        items ? json_pack("{s:b,s:b,s:s,s:s,s:s,s:f,s:I,s:O,s:b,s:b}", "ok", ok, "writable",
                          config->writable, "message", config->message, "id", id, "selected",
                          config->selected, "requests_per_second", config->requests_per_second,
                          "request_burst", (json_int_t)config->request_burst, "profiles", items,
                          "desktop_notifications", config->desktop_notifications,
                          "notification_preview", config->notification_preview)
              : NULL;
    json_decref(items);
    profiles_destroy(config);
    int status = emit(result);
    curl_global_cleanup();
    return status;
}
