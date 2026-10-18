#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "net/realtime/websocket.h"
#include "net/clock.h"
#include "net/rate_limit.h"
#include "net/tls.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HTTP_LIMIT (64U * 1024U)
#define HEARTBEAT_INTERVAL_MS 30000U
#define ACK_TIMEOUT_MS 10000U
#define CLOSE_TIMEOUT_MS 250U

typedef struct {
    char *data;
    size_t length, offset;
    unsigned flags;
    bool heartbeat;
    uint64_t deadline;
} output_item;

struct ws_client {
    CURLSH *cookies; /* Borrowed; only this context shares the jar. */
    CURL *easy;
    CURLM *multi;
    bool attached;
    ws_state state;
    long status;
    unsigned retry_after;
    size_t header_bytes, body_bytes;
    bool http_overflow;
    char message[160];
    char *incoming;
    size_t incoming_length, frame_offset;
    bool receiving;
    uint64_t incoming_deadline;
    char close_payload[125];
    size_t close_length;
    void (*observer)(void *, const json_t *);
    void *observer_context;
    json_t *events[WS_QUEUE_LIMIT];
    size_t event_sizes[WS_QUEUE_LIMIT];
    size_t event_head, event_count, event_bytes;
    output_item output[WS_QUEUE_LIMIT];
    size_t output_head, output_count, output_bytes;
    uint64_t heartbeat_at, ack_deadline, close_deadline;
    bool heartbeat_queued, peer_closed;
};

bool ws_supported(void)
{
    const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
    bool plain = false, secure = false;
    if (!version || !version->protocols || !(version->features & CURL_VERSION_ASYNCHDNS) ||
        !(version->features & CURL_VERSION_SSL))
        return false;
    for (const char *const *p = version->protocols; *p; ++p) {
        if (strcmp(*p, "ws") == 0)
            plain = true;
        if (strcmp(*p, "wss") == 0)
            secure = true;
    }
    return plain && secure;
}

static void clear_output(ws_client *client)
{
    for (size_t i = 0; i < WS_QUEUE_LIMIT; ++i) {
        free(client->output[i].data);
        client->output[i] = (output_item){0};
    }
    client->output_head = client->output_count = client->output_bytes = 0;
    client->heartbeat_queued = false;
}

static void release(ws_client *client)
{
    if (client->attached) {
        curl_multi_remove_handle(client->multi, client->easy);
        client->attached = false;
    }
    if (client->easy)
        curl_easy_cleanup(client->easy);
    client->easy = NULL;
    free(client->incoming);
    client->incoming = NULL;
    client->incoming_length = client->frame_offset = 0;
    client->receiving = false;
    client->incoming_deadline = 0;
    client->close_length = 0;
    for (size_t i = 0; i < WS_QUEUE_LIMIT; ++i) {
        json_decref(client->events[i]);
        client->events[i] = NULL;
    }
    client->event_head = client->event_count = client->event_bytes = 0;
    clear_output(client);
    client->ack_deadline = client->close_deadline = 0;
}

static void fail(ws_client *client, const char *message)
{
    release(client);
    client->state = WS_ERROR;
    snprintf(client->message, sizeof(client->message), "%s", message);
}

ws_client *ws_create(CURLSH *cookies)
{
    if (!cookies || !ws_supported())
        return NULL;
    ws_client *client = calloc(1, sizeof(*client));
    if (!client)
        return NULL;
    client->cookies = cookies;
    client->multi = curl_multi_init();
    if (!client->multi) {
        free(client);
        return NULL;
    }
    return client;
}

void ws_abort(ws_client *client)
{
    release(client);
    client->state = WS_IDLE;
    client->status = 0;
    client->message[0] = '\0';
}

static size_t receive_body(char *data, size_t size, size_t count, void *context)
{
    (void)data;
    ws_client *client = context;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    if (bytes > HTTP_LIMIT - client->body_bytes) {
        client->http_overflow = true;
        return 0;
    }
    client->body_bytes += bytes;
    return bytes; /* Error response bodies are never retained or logged. */
}

static size_t receive_header(char *data, size_t size, size_t count, void *context)
{
    ws_client *client = context;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    if (bytes > HTTP_LIMIT - client->header_bytes) {
        client->http_overflow = true;
        return 0;
    }
    client->header_bytes += bytes;
    if (bytes >= 5 && strncasecmp(data, "HTTP/", 5) == 0)
        client->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    if (bytes > 12 && strncasecmp(data, "Retry-After:", 12) == 0)
        client->retry_after = net_rate_retry_after(data + 12, bytes - 12);
    return bytes;
}

static char *endpoint(const char *base)
{
    CURLU *url = curl_url();
    char *scheme = NULL, *path = NULL, *address = NULL;
    if (!url || !base || strlen(base) > 2048 ||
        curl_url_set(url, CURLUPART_URL, base, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_PATH, &path, 0) != CURLUE_OK)
        goto done;
    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0)
        goto done;
    size_t length = strlen(path);
    while (length && path[length - 1] == '/')
        --length;
    char *next = malloc(length + 4);
    if (!next)
        goto done;
    memcpy(next, path, length);
    memcpy(next + length, "/ws", 4);
    CURLUcode result = curl_url_set(url, CURLUPART_PATH, next, 0);
    free(next);
    if (result != CURLUE_OK ||
        curl_url_set(url, CURLUPART_SCHEME, strcmp(scheme, "https") == 0 ? "wss" : "ws", 0) !=
            CURLUE_OK)
        goto done;
    curl_url_get(url, CURLUPART_URL, &address, 0);
done:
    curl_free(scheme);
    curl_free(path);
    curl_url_cleanup(url);
    return address;
}

bool ws_connect(ws_client *client, const char *base)
{
    if (client->state == WS_QUEUED || client->state == WS_CONNECTING || client->state == WS_OPEN ||
        client->state == WS_CLOSING)
        return false;
    ws_abort(client);
    client->header_bytes = client->body_bytes = 0;
    client->http_overflow = false;
    client->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    client->peer_closed = false;
    char *url = endpoint(base); /* Base is validated by the owning auth context. */
    if (!url) {
        fail(client, "Não foi possível preparar o endereço de tempo real.");
        return false;
    }
    client->easy = curl_easy_init();
    if (!client->easy) {
        curl_free(url);
        fail(client, "Não foi possível preparar o tempo real.");
        return false;
    }
#define SET(option, value)                                                                         \
    do {                                                                                           \
        if (curl_easy_setopt(client->easy, option, value) != CURLE_OK)                             \
            goto error;                                                                            \
    } while (0)
    SET(CURLOPT_URL, url);
    SET(CURLOPT_SHARE, client->cookies);
    SET(CURLOPT_COOKIEFILE, "");
    SET(CURLOPT_CONNECT_ONLY, 2L);
    SET(CURLOPT_PROTOCOLS_STR, "ws,wss");
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    SET(CURLOPT_TIMEOUT_MS, 15000L);
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_USERAGENT, "papo-tui/" PAPO_VERSION);
    SET(CURLOPT_HEADERFUNCTION, receive_header);
    SET(CURLOPT_HEADERDATA, client);
    SET(CURLOPT_WRITEFUNCTION, receive_body);
    SET(CURLOPT_WRITEDATA, client);
    const char *ca = net_ca_bundle();
    if (ca && *ca)
        SET(CURLOPT_CAINFO, ca);
#undef SET
    curl_free(url);
    client->state = WS_QUEUED;
    snprintf(client->message, sizeof(client->message), "Conectando tempo real…");
    return true;
error:
    curl_free(url);
    fail(client, "Não foi possível preparar o tempo real.");
    return false;
}

static bool enqueue(ws_client *client, char *data, size_t length, unsigned flags, bool heartbeat)
{
    uint64_t now;
    if (!net_now_ms(&now)) {
        free(data);
        fail(client, "Não foi possível acompanhar o envio de tempo real.");
        return false;
    }
    if (length > WS_MESSAGE_LIMIT || client->output_count == WS_QUEUE_LIMIT ||
        length > WS_QUEUE_BYTES - client->output_bytes) {
        free(data);
        fail(client, "A fila de envio de tempo real excedeu o limite.");
        return false;
    }
    size_t index = (client->output_head + client->output_count) % WS_QUEUE_LIMIT;
    client->output[index] = (output_item){data, length, 0, flags, heartbeat, now + 10000};
    ++client->output_count;
    client->output_bytes += length;
    if (heartbeat)
        client->heartbeat_queued = true;
    return true;
}

bool ws_send(ws_client *client, const json_t *event)
{
    if (client->state != WS_OPEN || !json_is_object(event))
        return false;
    const char *type = json_string_value(json_object_get(event, "type"));
    if (!type || !*type)
        return false;
    char *data = json_dumps(event, JSON_COMPACT);
    if (!data) {
        fail(client, "Não foi possível preparar o evento de tempo real.");
        return false;
    }
    return enqueue(client, data, strlen(data), CURLWS_TEXT, strcmp(type, "heartbeat") == 0);
}

static void begin_close(ws_client *client, const char *payload, size_t length, uint64_t now)
{
    clear_output(client);
    char *data = malloc(length ? length : 1);
    if (!data) {
        fail(client, "Não foi possível encerrar o tempo real.");
        return;
    }
    if (length)
        memcpy(data, payload, length);
    if (!enqueue(client, data, length, CURLWS_CLOSE, false))
        return;
    client->state = WS_CLOSING;
    client->close_deadline = now + CLOSE_TIMEOUT_MS;
    snprintf(client->message, sizeof(client->message), "Encerrando tempo real…");
}

void ws_close(ws_client *client)
{
    if (client->state == WS_OPEN) {
        uint64_t now;
        if (!net_now_ms(&now)) {
            ws_abort(client);
            return;
        }
        const char normal[] = {3, (char)232}; /* RFC 6455 close code 1000. */
        begin_close(client, normal, sizeof(normal), now);
    } else if (client->state == WS_CONNECTING || client->state == WS_QUEUED) {
        release(client);
        client->state = WS_CLOSED;
    }
}

static void flush(ws_client *client, uint64_t now)
{
    /* Bounded work per tick, including partial writes and CURLE_AGAIN. */
    for (unsigned work = 0; work < 4 && client->output_count; ++work) {
        output_item *item = &client->output[client->output_head];
        if (now >= item->deadline) {
            fail(client, "O envio de tempo real excedeu o prazo.");
            return;
        }
        size_t sent = 0;
        CURLcode result = curl_ws_send(client->easy, item->data + item->offset,
                                       item->length - item->offset, &sent, 0, item->flags);
        if (sent > item->length - item->offset) {
            fail(client, "Falha ao enviar o evento de tempo real.");
            return;
        }
        item->offset += sent;
        if (result != CURLE_OK && result != CURLE_AGAIN) {
            fail(client, "A conexão de tempo real falhou durante o envio.");
            return;
        }
        if (result == CURLE_OK && item->offset == item->length) {
            if (item->heartbeat) {
                client->heartbeat_queued = false;
                client->ack_deadline = now + ACK_TIMEOUT_MS;
                client->heartbeat_at = now + HEARTBEAT_INTERVAL_MS;
            }
            client->output_bytes -= item->length;
            free(item->data);
            *item = (output_item){0};
            client->output_head = (client->output_head + 1) % WS_QUEUE_LIMIT;
            --client->output_count;
        } else
            break;
    }
}

static bool finish_event(ws_client *client)
{
    json_error_t error;
    json_t *event = json_loadb(client->incoming ? client->incoming : "", client->incoming_length,
                               JSON_REJECT_DUPLICATES, &error);
    const char *type = json_string_value(json_object_get(event, "type"));
    if (!json_is_object(event) || !type || !*type) {
        json_decref(event);
        fail(client, "O servidor enviou um evento de tempo real inválido.");
        return false;
    }
    if (client->event_count == WS_QUEUE_LIMIT ||
        client->incoming_length > WS_QUEUE_BYTES - client->event_bytes) {
        json_decref(event);
        fail(client, "A fila de eventos de tempo real excedeu o limite.");
        return false;
    }
    if (client->observer)
        client->observer(client->observer_context, event);
    if (strcmp(type, "heartbeat_ack") == 0)
        client->ack_deadline = 0;
    size_t index = (client->event_head + client->event_count) % WS_QUEUE_LIMIT;
    client->events[index] = event;
    client->event_sizes[index] = client->incoming_length;
    client->event_bytes += client->incoming_length;
    ++client->event_count;
    free(client->incoming);
    client->incoming = NULL;
    client->incoming_length = client->frame_offset = 0;
    client->receiving = false;
    client->incoming_deadline = 0;
    return true;
}

static void read_events(ws_client *client, uint64_t now)
{
    for (unsigned work = 0; work < 16; ++work) {
        char buffer[4096];
        size_t bytes = 0;
        const struct curl_ws_frame *frame = NULL;
        CURLcode result = curl_ws_recv(client->easy, buffer, sizeof(buffer), &bytes, &frame);
        if (result == CURLE_AGAIN)
            return;
        if (result != CURLE_OK || !frame) {
            if (client->state == WS_CLOSING && result == CURLE_GOT_NOTHING) {
                release(client);
                client->state = WS_CLOSED;
            } else
                fail(client, "A conexão de tempo real foi interrompida.");
            return;
        }
        /* Consume metadata before the next libcurl call invalidates it. */
        int flags = frame->flags;
        curl_off_t remaining = frame->bytesleft, offset = frame->offset;
        if (flags & CURLWS_CLOSE) {
            if (bytes > sizeof(client->close_payload) - client->close_length || offset < 0 ||
                (uint64_t)offset != client->close_length) {
                fail(client, "O servidor enviou um encerramento de tempo real inválido.");
                return;
            }
            memcpy(client->close_payload + client->close_length, buffer, bytes);
            client->close_length += bytes;
            if (remaining != 0)
                continue;
            client->peer_closed = true;
            if (client->state != WS_CLOSING)
                begin_close(client, client->close_payload, client->close_length, now);
            return;
        }
        if (flags & (CURLWS_PING | CURLWS_PONG))
            continue; /* libcurl replies to ping. */
        if (client->state == WS_CLOSING)
            continue;
        if ((flags & CURLWS_BINARY) || (!(flags & CURLWS_TEXT) && !client->receiving)) {
            fail(client, "O servidor enviou um evento de tempo real incompatível.");
            return;
        }
        if (offset < 0 || remaining < 0 || (uint64_t)offset != client->frame_offset ||
            bytes > WS_MESSAGE_LIMIT - client->incoming_length ||
            (uint64_t)remaining > WS_MESSAGE_LIMIT - client->incoming_length - bytes) {
            fail(client, "O evento de tempo real excedeu o limite ou chegou incompleto.");
            return;
        }
        if (!client->receiving)
            client->incoming_deadline = now + 10000;
        client->receiving = true;
        char *data = realloc(client->incoming, client->incoming_length + bytes + 1);
        if (!data) {
            fail(client, "Não foi possível receber o evento de tempo real.");
            return;
        }
        client->incoming = data;
        memcpy(data + client->incoming_length, buffer, bytes);
        client->incoming_length += bytes;
        data[client->incoming_length] = '\0';
        client->frame_offset += bytes;
        if (remaining == 0) {
            client->frame_offset = 0;
            if (!(flags & CURLWS_CONT) && !finish_event(client))
                return;
        }
    }
}

void ws_poll(ws_client *client)
{
    if (client->state == WS_IDLE || client->state == WS_CLOSED || client->state == WS_ERROR)
        return;
    uint64_t now;
    if (!net_now_ms(&now)) {
        fail(client, "Não foi possível acompanhar a conexão de tempo real.");
        return;
    }
    if (client->state == WS_QUEUED) {
        int admitted = net_rate_try_acquire();
        if (!admitted)
            return;
        if (admitted < 0 || curl_multi_add_handle(client->multi, client->easy) != CURLM_OK) {
            fail(client, "Não foi possível iniciar o tempo real.");
            return;
        }
        client->attached = true;
        client->state = WS_CONNECTING;
    }
    if (client->state == WS_CONNECTING) {
        int running, remaining;
        if (curl_multi_perform(client->multi, &running) != CURLM_OK) {
            fail(client, "Não foi possível conectar o tempo real.");
            return;
        }
        CURLMsg *done;
        while ((done = curl_multi_info_read(client->multi, &remaining))) {
            if (done->msg != CURLMSG_DONE)
                continue;
            CURLcode result = done->data.result;
            curl_easy_getinfo(client->easy, CURLINFO_RESPONSE_CODE, &client->status);
            if (client->status == 429)
                net_rate_defer(client->retry_after);
            if (result != CURLE_OK || client->status != 101) {
                const char *message = "Não foi possível conectar o tempo real.";
                if (client->http_overflow)
                    message = "A resposta de tempo real excedeu o limite.";
                else if (client->status == 401 || client->status == 403)
                    message = "A sessão não foi autorizada para tempo real.";
                else if (client->status == 429)
                    message = "Limite de requisições atingido no tempo real. Aguarde.";
                else if (client->status >= 300 && client->status < 400)
                    message = "O tempo real redirecionou a conexão. Confira a URL.";
                else if (result == CURLE_PEER_FAILED_VERIFICATION ||
                         result == CURLE_SSL_CONNECT_ERROR)
                    message = "Falha ao verificar a conexão segura de tempo real.";
                else if (result == CURLE_OPERATION_TIMEDOUT)
                    message = "O tempo real demorou para responder.";
                fail(client, message);
                return;
            }
            /* CONNECT_ONLY handles must stay in their multi until close. */
            client->state = WS_OPEN;
            client->heartbeat_at = now;
            snprintf(client->message, sizeof(client->message), "Tempo real conectado.");
            break;
        }
    }
    if (client->state == WS_OPEN) {
        if (client->incoming_deadline && now >= client->incoming_deadline) {
            fail(client, "O evento de tempo real não chegou completo no prazo.");
            return;
        }
        if (client->ack_deadline && now >= client->ack_deadline) {
            fail(client, "O tempo real não confirmou a conexão no prazo.");
            return;
        }
        if (!client->ack_deadline && !client->heartbeat_queued && now >= client->heartbeat_at) {
            json_t *heartbeat = json_pack("{s:s}", "type", "heartbeat");
            if (!heartbeat) {
                fail(client, "Não foi possível preparar o heartbeat.");
                return;
            }
            bool queued = ws_send(client, heartbeat);
            json_decref(heartbeat);
            if (!queued)
                return;
        }
    }
    if (client->state == WS_OPEN || client->state == WS_CLOSING) {
        flush(client, now);
        if (client->state == WS_ERROR)
            return;
        read_events(client, now);
        if (client->state == WS_CLOSING &&
            ((client->peer_closed && !client->output_count) || now >= client->close_deadline)) {
            release(client);
            client->state = WS_CLOSED;
            snprintf(client->message, sizeof(client->message), "Tempo real encerrado.");
        }
    }
}

json_t *ws_pop(ws_client *client)
{
    if (!client->event_count)
        return NULL;
    size_t index = client->event_head;
    json_t *event = client->events[index];
    client->events[index] = NULL;
    client->event_bytes -= client->event_sizes[index];
    client->event_head = (index + 1) % WS_QUEUE_LIMIT;
    --client->event_count;
    return event;
}

void ws_destroy(ws_client *client)
{
    if (!client)
        return;
    ws_close(client);
    if (client->state == WS_CLOSING) {
        uint64_t now;
        if (net_now_ms(&now))
            flush(client, now); /* One bounded best-effort close. */
    }
    release(client);
    curl_multi_cleanup(client->multi);
    free(client);
}

ws_state ws_get_state(const ws_client *client)
{
    return client->state;
}
const char *ws_message(const ws_client *client)
{
    return client->message;
}
long ws_http_status(const ws_client *client)
{
    return client->status;
}
size_t ws_queued_events(const ws_client *client)
{
    return client->event_count;
}

void ws_observe(ws_client *client, void (*observer)(void *, const json_t *), void *context)
{
    client->observer = observer;
    client->observer_context = context;
}
