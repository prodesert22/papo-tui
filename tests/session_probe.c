#define _POSIX_C_SOURCE 200809L
#include "net/auth.h"
#include "net/clock.h"
#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void poll_for(auth_client *client, unsigned milliseconds)
{
    struct timespec pause = {.tv_nsec = 1000000};
    uint64_t start, now;
    if (!net_now_ms(&start)) exit(2);
    do { auth_poll(client); nanosleep(&pause, NULL); if (!net_now_ms(&now)) exit(2); }
    while (now - start < milliseconds);
}

static void finish(auth_client *client)
{
    uint64_t start, now;
    if (!net_now_ms(&start)) exit(2);
    while (auth_busy(client)) {
        poll_for(client, 1);
        if (!net_now_ms(&now) || now - start > 20000) exit(2);
    }
}

static const char *setting(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value ? value : fallback;
}

int main(int argc, char **argv)
{
    if (argc < 3 || !setlocale(LC_ALL, "") || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    auth_client *client = auth_create();
    if (!client) return 1;
    const char *mode = argv[2];
    const char *username = setting("PAPO_PROBE_USERNAME", "josé");
    const char *password = setting("PAPO_PROBE_PASSWORD", "Synthetic!123");
    const char *server_password = setting("PAPO_PROBE_SERVER_PASSWORD", "");
    bool registered = false, initial = false, concurrent_blocked = false, ws_open = false, logout_started = false;
    uint64_t generation = 0, response_generation = 0;
    long status = -1;
    if (strncmp(mode, "register", 8) == 0) {
        auth_register_with_policy(client, argv[1], username, password, server_password, true);
        finish(client);
        registered = auth_get_state(client) == AUTH_REGISTERED && !auth_has_session(client) && !*auth_user_id(client);
        if (strcmp(mode, "register-login") == 0 && registered) {
            auth_start(client, argv[1], username, password, server_password);
            finish(client);
        }
    } else {
        auth_start(client, argv[1], username, password, server_password);
        finish(client);
    }
    initial = auth_has_session(client);
    generation = auth_context_generation(client);
    if (initial && (strcmp(mode, "refresh") == 0 || strcmp(mode, "automatic") == 0 || strcmp(mode, "logout-refresh") == 0 || strcmp(mode, "ws-refresh") == 0 || strcmp(mode, "controlled-refresh") == 0)) {
        if (strcmp(mode, "ws-refresh") == 0) {
            auth_connect_ws(client);
            for (unsigned i = 0; i < 2000 && auth_ws_state(client) != WS_OPEN; ++i) poll_for(client, 1);
            ws_open = auth_ws_state(client) == WS_OPEN;
        }
        if (strcmp(mode, "controlled-refresh") == 0) {
            puts("{\"awaiting_control\":true}"); fflush(stdout);
            char control[16];
            if (!fgets(control, sizeof(control), stdin)) return 2;
        }
        auth_refresh(client);
        concurrent_blocked = !auth_refresh(client) && !auth_get_json(client, "/auth/whoami");
        if (strcmp(mode, "logout-refresh") == 0) {
            poll_for(client, 30);
            logout_started = auth_logout(client) && !auth_has_session(client);
        }
        finish(client);
        if (strcmp(mode, "automatic") == 0 && auth_has_session(client)) poll_for(client, 1800);
        if (strcmp(mode, "ws-refresh") == 0) {
            poll_for(client, 30);
            ws_open = ws_open && auth_ws_state(client) == WS_OPEN;
        }
    }
    if (initial && (strcmp(mode, "api") == 0 || strcmp(mode, "whoami") == 0)) {
        auth_get_json(client, strcmp(mode, "whoami") == 0 ? "/auth/whoami" : "/fixture/read");
        concurrent_blocked = !auth_refresh(client) && !auth_get_json(client, "/auth/whoami");
        finish(client);
        json_t *body = auth_take_response(client, &status, &response_generation);
        json_decref(body);
    }
    if (initial && strcmp(mode, "logout") == 0) {
        logout_started = auth_logout(client) && !auth_has_session(client);
        finish(client);
    }
    if (initial && strcmp(mode, "rate") == 0) {
        for (unsigned i = 0; i < 14 && auth_has_session(client); ++i) {
            auth_refresh(client);
            finish(client);
        }
    }
    if (initial && strcmp(mode, "switch") == 0) {
        auth_cancel(client);
        auth_start(client, argv[1], username, password, server_password);
        finish(client);
    }
    json_t *result = json_pack("{s:b,s:b,s:b,s:b,s:b,s:b,s:i,s:i,s:I,s:I,s:I,s:s,s:s}",
        "registered", registered, "initial", initial, "session", auth_has_session(client),
        "concurrent_blocked", concurrent_blocked, "ws_open", ws_open, "logout_started", logout_started,
        "state", (int)auth_get_state(client), "input_error", (int)auth_error_input(client),
        "status", (json_int_t)status, "generation", (json_int_t)generation,
        "response_generation", (json_int_t)response_generation, "identity", auth_user_id(client), "message", auth_message(client));
    json_object_set_new(result, "final_generation", json_integer((json_int_t)auth_context_generation(client)));
    json_dumpf(result, stdout, JSON_COMPACT); putchar('\n'); json_decref(result);
    auth_destroy(client);
    curl_global_cleanup();
    return 0;
}
