#define _POSIX_C_SOURCE 200809L
#include "net/http/download.h"
#include "net/rate_limit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static void tick(auth_client *auth, media_download *download)
{
    auth_poll(auth);
    media_download_poll(download);
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
int main(int argc, char **argv)
{
    if (argc != 3 || curl_global_init(CURL_GLOBAL_DEFAULT))
        return 1;
    auth_client *auth = auth_create();
    const char *username = getenv("PAPO_PROBE_USERNAME"), *password = getenv("PAPO_PROBE_PASSWORD");
    if (!auth || !auth_start(auth, argv[1], username ? username : "josé",
                             password ? password : "Synthetic!123", ""))
        return 2;
    media_download *m = media_download_create(auth);
    if (!m)
        return 3;
    for (int i = 0; auth_busy(auth) && i < 10000; ++i)
        tick(auth, m);
    if (!auth_has_session(auth))
        return 4;
    const char *id = getenv("PAPO_MEDIA_ID");
    if (!id)
        id = "00000000-0000-4000-8000-000000000901";
    bool video = !strcmp(argv[2], "video") || !strcmp(argv[2], "attachment-video");
    auth_media_kind kind = !strcmp(argv[2], "embed")   ? AUTH_MEDIA_EMBED
                           : !strcmp(argv[2], "video") ? AUTH_MEDIA_VIDEO
                                                       : AUTH_MEDIA_ATTACHMENT;
    if (!strcmp(argv[2], "invalid"))
        id = "../../passwords";
    bool avatar = !strncmp(argv[2], "avatar", 6);
    if (!strcmp(argv[2], "budget") || !strcmp(argv[2], "avatar-budget")) {
        net_rate_configure(.01, 1);
        while (net_rate_try_acquire() == 1) {
        }
    }
    const char *reference = getenv("PAPO_AVATAR_REFERENCE");
    if (!reference)
        reference = "/webhooks/00000000-0000-4000-8000-000000000901/avatar";
    bool requested = avatar ? media_download_avatar_request(m, reference)
                            : media_download_request(m, kind, id, video);
    if (!strcmp(argv[2], "invalid") || !strcmp(argv[2], "avatar-invalid")) {
        printf("rejected=%d\n", !requested);
        goto done;
    }
    if (!requested)
        return 5;
    tick(auth, m);
    if (!strcmp(argv[2], "cancel") || !strcmp(argv[2], "budget") ||
        !strcmp(argv[2], "avatar-budget")) {
        media_download_cancel(m);
        printf("cancelled=%d session=%d\n", media_download_state(m) == MEDIA_IDLE,
               auth_has_session(auth));
        goto done;
    }
    if (!strcmp(argv[2], "logout")) {
        auth_logout(auth);
        tick(auth, m);
        printf("discarded=%d\n", media_download_state(m) == MEDIA_IDLE);
        goto done;
    }
    bool read = auth_get_json(auth, "/server"), independent = false;
    for (int i = 0; i < 10000; ++i) {
        tick(auth, m);
        if (auth_response_ready(auth)) {
            long status;
            json_t *body = auth_take_response(auth, &status, NULL);
            independent = read && status == 200;
            json_decref(body);
        }
        media_state state = media_download_state(m);
        if (state == MEDIA_READY || state == MEDIA_FAILED)
            break;
    }
    char *path = media_download_take_file(m);
    struct stat file, directory;
    bool permissions = false;
    if (path && !stat(path, &file)) {
        char *parent = strdup(path);
        char *last = strrchr(parent, '/');
        *last = 0;
        permissions = (file.st_mode & 0777) == 0600 && !stat(parent, &directory) &&
                      (directory.st_mode & 0777) == 0700;
        free(parent);
    }
    printf("ready=%d failed=%d status=%ld bytes=%zu private=%d independent=%d session=%d\n",
           path != NULL, media_download_state(m) == MEDIA_FAILED, media_download_http_status(m),
           media_download_bytes(m), permissions, independent, auth_has_session(auth));
    if (path) {
        unlink(path);
        free(path);
    }
done:
    media_download_destroy(m);
    auth_destroy(auth);
    curl_global_cleanup();
    return 0;
}
