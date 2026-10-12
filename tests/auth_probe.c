#define _POSIX_C_SOURCE 200809L
#include "net/auth.h"
#include "net/rate_limit.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void finish(auth_client *client)
{
    struct timespec wait = {.tv_nsec = 1000000};
    while (auth_busy(client)) {
        auth_poll(client);
        nanosleep(&wait, NULL);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    if (argc > 2 && strcmp(argv[2], "normalize") == 0) {
        char error[192];
        char *base = auth_normalize_url(argv[1], error, sizeof(error));
        printf("%s\n", base ? base : error);
        int result = base ? 0 : 2;
        curl_free(base);
        curl_global_cleanup();
        return result;
    }
    auth_client *client = auth_create();
    if (!client) return 1;
    const char *mode = argc > 2 ? argv[2] : "";
    const char *username = getenv("PAPO_PROBE_USERNAME");
    const char *password = getenv("PAPO_PROBE_PASSWORD");
    const char *server_password = getenv("PAPO_PROBE_SERVER_PASSWORD");
    username = username ? username : "josé";
    password = password ? password : "Synthetic!123";
    server_password = server_password ? server_password : strcmp(mode, "private") == 0 ? "Server!123" : "";
    if (strcmp(mode, "queued-cancel") == 0) {
        while (net_rate_try_acquire() == 1) {}
        auth_start(client, argv[1], username, password, server_password);
        auth_poll(client);
        auth_cancel(client);
        printf("cancelled=%d\n", auth_get_state(client) == AUTH_IDLE);
        auth_destroy(client);
        curl_global_cleanup();
        return 0;
    }
    auth_start(client, argv[1], username, password, server_password);
    if (strcmp(mode, "cancel") == 0) {
        auth_poll(client);
        auth_cancel(client);
        printf("cancelled=%d\n", auth_get_state(client) == AUTH_IDLE);
    } else {
        finish(client);
        printf("ready=%d\n%s\n", auth_get_state(client) == AUTH_READY, auth_message(client));
        if (strcmp(mode, "rate") == 0) {
            for (unsigned i = 0; i < 12; ++i) {
                if (auth_get_state(client) != AUTH_READY) return 2;
                auth_logout(client);
                finish(client);
                auth_destroy(client);
                client = auth_create();
                if (!client) return 2;
                auth_start(client, argv[1], username, password, server_password);
                finish(client);
            }
            printf("rate_ready=%d\n", auth_get_state(client) == AUTH_READY);
        }
        if (strcmp(mode, "retry") == 0) {
            auth_destroy(client);
            client = auth_create();
            if (!client) return 2;
            auth_start(client, argv[1], username, password, server_password);
            finish(client);
            printf("retry_ready=%d\n", auth_get_state(client) == AUTH_READY);
        }
        if ((strcmp(mode, "logout") == 0 || strcmp(mode, "verify") == 0) && auth_get_state(client) == AUTH_READY) {
            auth_logout(client);
            finish(client);
            printf("logout=%d\n", auth_get_state(client) == AUTH_IDLE);
            if (strcmp(mode, "logout") == 0) {
                auth_start(client, argv[1], username, password, server_password);
                finish(client);
                printf("again=%d\n", auth_get_state(client) == AUTH_READY);
            }
        }
    }
    auth_destroy(client);
    curl_global_cleanup();
    return 0;
}
