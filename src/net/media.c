#define _POSIX_C_SOURCE 200809L
#include "net/media.h"
#include "net/rate_limit.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct media_download {
    auth_client *auth;
    CURLM *multi;
    CURL *easy;
    FILE *file;
    char *directory, *path;
    char id[37], mime[80];
    auth_media_kind kind;
    media_state state;
    size_t bytes, limit;
    uint64_t generation;
    unsigned retry_after;
    long status;
    bool attached, overflow, video, public_avatar;
    const char *error;
};
int media_download_temp(media_download *m, char **path)
{
    if (!m || !path) return -1;
    *path = NULL;
    size_t length = strlen(m->directory) + 20;
    char *name = malloc(length);
    if (!name) return -1;
    snprintf(name, length, "%s/blob-XXXXXX", m->directory);
    int fd = mkstemp(name);
    if (fd < 0) { free(name); return -1; }
    *path = name;
    return fd;
}
static void release_transfer(media_download *m)
{
    if (m->attached) curl_multi_remove_handle(m->multi, m->easy);
    m->attached = false;
    curl_easy_cleanup(m->easy); m->easy = NULL;
    if (m->file) fclose(m->file);
    m->file = NULL;
}
void media_download_cancel(media_download *m)
{
    if (!m) return;
    release_transfer(m);
    if (m->path) { unlink(m->path); free(m->path); m->path = NULL; }
    m->state = MEDIA_IDLE; m->bytes = 0;
}
media_download *media_download_create(auth_client *auth)
{
    media_download *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->auth = auth;
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    size_t length = strlen(tmp) + 32;
    m->directory = malloc(length);
    m->multi = curl_multi_init();
    if (!m->directory || !m->multi) goto error;
    snprintf(m->directory, length, "%s/papo-media-XXXXXX", tmp);
    if (!mkdtemp(m->directory)) goto error;
    return m;
error:
    curl_multi_cleanup(m->multi); free(m->directory); free(m); return NULL;
}
void media_download_destroy(media_download *m)
{
    if (!m) return;
    media_download_cancel(m);
    curl_multi_cleanup(m->multi);
    rmdir(m->directory); free(m->directory); free(m);
}
static size_t receive(char *data, size_t size, size_t count, void *context)
{
    media_download *m = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    if (bytes > m->limit - m->bytes) { m->overflow = true; return 0; }
    if (fwrite(data, 1, bytes, m->file) != bytes) return 0;
    m->bytes += bytes;
    return bytes;
}
static size_t header(char *data, size_t size, size_t count, void *context)
{
    media_download *m = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    if (bytes >= 5 && !strncasecmp(data, "HTTP/", 5)) m->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    if (bytes > 12 && !strncasecmp(data, "Retry-After:", 12))
        m->retry_after = net_rate_retry_after(data + 12, bytes - 12);
    return bytes;
}
static bool begin_request(media_download *m, CURL *easy, auth_media_kind kind, bool video, bool avatar)
{
    media_download_cancel(m);
    m->easy = easy;
    if (!m->easy) return false;
    m->kind = kind; m->video = video; m->public_avatar = avatar;
    m->generation = auth_context_generation(m->auth);
    m->limit = avatar ? MEDIA_AVATAR_LIMIT : kind == AUTH_MEDIA_EMBED ? MEDIA_EMBED_LIMIT : video ? MEDIA_VIDEO_LIMIT : MEDIA_IMAGE_LIMIT;
    m->overflow = false; m->status = 0; m->mime[0] = 0; m->error = NULL;
    int fd = media_download_temp(m, &m->path);
    if (fd < 0 || !(m->file = fdopen(fd, "wb"))) {
        if (fd >= 0) close(fd);
        media_download_cancel(m); return false;
    }
    if (curl_easy_setopt(m->easy, CURLOPT_WRITEFUNCTION, receive) != CURLE_OK ||
        curl_easy_setopt(m->easy, CURLOPT_WRITEDATA, m) != CURLE_OK ||
        curl_easy_setopt(m->easy, CURLOPT_HEADERFUNCTION, header) != CURLE_OK ||
        curl_easy_setopt(m->easy, CURLOPT_HEADERDATA, m) != CURLE_OK ||
        curl_easy_setopt(m->easy, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)m->limit) != CURLE_OK) {
        media_download_cancel(m); return false;
    }
    m->state = MEDIA_WAITING;
    return true;
}
bool media_download_request(media_download *m, auth_media_kind kind, const char *id, bool video)
{
    if (!m || !auth_has_session(m->auth) || !id || strlen(id) != 36) return false;
    return begin_request(m, auth_media_easy(m->auth, kind, id), kind, video, false);
}
bool media_download_avatar_request(media_download *m, const char *reference)
{
    if (!m) return false;
    return begin_request(m, auth_avatar_easy(m->auth, reference), AUTH_MEDIA_ATTACHMENT, false, true);
}
static void finish(media_download *m, CURLcode result)
{
    curl_easy_getinfo(m->easy, CURLINFO_RESPONSE_CODE, &m->status);
    char *mime = NULL;
    curl_easy_getinfo(m->easy, CURLINFO_CONTENT_TYPE, &mime);
    if (mime) {
        size_t n = strcspn(mime, "; \r\n");
        if (n < sizeof(m->mime)) { memcpy(m->mime, mime, n); m->mime[n] = 0; }
    }
    bool written = m->file && fflush(m->file) == 0 && !ferror(m->file);
    release_transfer(m);
    bool type = m->kind == AUTH_MEDIA_EMBED ? !strcmp(m->mime, "application/json") :
        m->video ? (!strcmp(m->mime, "video/mp4") || !strcmp(m->mime, "video/webm") || !strcmp(m->mime, "video/ogg")) :
        (!strcmp(m->mime, "image/png") || !strcmp(m->mime, "image/jpeg") || !strcmp(m->mime, "image/webp") || !strcmp(m->mime, "image/gif"));
    if (result == CURLE_OK && m->status == 200 && written && type && m->bytes) { m->state = MEDIA_READY; return; }
    if (m->status == 429) { net_rate_defer(m->retry_after); m->error = "Limite de requisições. Aguarde e tente novamente."; }
    else if (m->status == 401) {
        m->error = "Sessão rejeitada. Aguarde a revalidação e tente novamente.";
        if (!m->public_avatar && !auth_busy(m->auth)) auth_refresh(m->auth);
    } else if (m->status == 403 || m->status == 404) m->error = "Mídia indisponível ou sem permissão.";
    else if (m->overflow || result == CURLE_FILESIZE_EXCEEDED) m->error = "Mídia excede o limite de tamanho.";
    else if (result == CURLE_PEER_FAILED_VERIFICATION) m->error = "Certificado TLS da mídia não é confiável.";
    else if (result == CURLE_OK && !type) m->error = "Formato de mídia não suportado.";
    else m->error = "Não foi possível carregar a mídia. Tente novamente.";
    if (m->path) { unlink(m->path); free(m->path); m->path = NULL; }
    m->state = MEDIA_FAILED;
}
bool media_download_poll(media_download *m)
{
    if (!m || (m->state != MEDIA_WAITING && m->state != MEDIA_DOWNLOADING)) return false;
    if (m->generation != auth_context_generation(m->auth) || !auth_has_session(m->auth)) {
        media_download_cancel(m); return true;
    }
    if (m->state == MEDIA_WAITING) {
        if (auth_busy(m->auth)) return false;
        int admitted = net_rate_try_acquire();
        if (!admitted) return false;
        if (admitted < 0 || curl_multi_add_handle(m->multi, m->easy) != CURLM_OK) { finish(m, CURLE_FAILED_INIT); return true; }
        m->attached = true; m->state = MEDIA_DOWNLOADING;
    }
    size_t before = m->bytes;
    int running, remaining;
    if (curl_multi_perform(m->multi, &running) != CURLM_OK) { finish(m, CURLE_RECV_ERROR); return true; }
    CURLMsg *message;
    while ((message = curl_multi_info_read(m->multi, &remaining)))
        if (message->msg == CURLMSG_DONE) { finish(m, message->data.result); return true; }
    return before != m->bytes;
}
media_state media_download_state(const media_download *m) { return m ? m->state : MEDIA_FAILED; }
size_t media_download_bytes(const media_download *m) { return m ? m->bytes : 0; }
const char *media_download_error(const media_download *m) { return m && m->error ? m->error : "Mídia indisponível."; }
const char *media_download_mime(const media_download *m) { return m ? m->mime : ""; }
uint64_t media_download_generation(const media_download *m) { return m ? m->generation : 0; }
long media_download_http_status(const media_download *m) { return m ? m->status : 0; }
char *media_download_take_file(media_download *m)
{
    if (!m || m->state != MEDIA_READY) return NULL;
    char *path = m->path; m->path = NULL; m->state = MEDIA_IDLE;
    return path;
}
