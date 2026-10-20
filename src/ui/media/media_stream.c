#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/media_stream.h"
#include "platform/runtime.h"
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STREAM_TOKEN_BYTES 16U
#define STREAM_TOKEN_HEX (STREAM_TOKEN_BYTES * 2U)
#define STREAM_REQUEST_LIMIT 8192U
#define STREAM_CHUNK (64U * 1024U)
#define STREAM_WAIT_MS 200

typedef struct media_stream media_stream;
typedef struct stream_connection stream_connection;

struct media_stream {
    char *path, *url;
    char token[STREAM_TOKEN_HEX + 1];
    size_t available, total;
    bool complete, withdrawn;
    int refs; /* Table plus every connection currently serving it. */
};

struct media_stream_server {
    GSocket *socket;
    GSocketListener *listener;
    GThread *thread;
    guint16 port;
    GMutex lock;
    GCond wake;
    GHashTable *streams; /* path -> media_stream, owned */
    GHashTable *tokens;  /* token -> media_stream, borrowed */
    GPtrArray *connections;
    int refs;
    bool stopping;
};

struct stream_connection {
    media_stream_server *server;
    GSocketConnection *connection;
    char token[STREAM_TOKEN_HEX + 1];
};

static void server_unref(media_stream_server *server);

static void stream_free(media_stream *stream)
{
    if (!stream)
        return;
    g_free(stream->path);
    g_free(stream->url);
    g_free(stream);
}

static void stream_release(media_stream *stream)
{
    if (stream && g_atomic_int_dec_and_test(&stream->refs))
        stream_free(stream);
}

static bool stream_token(char out[STREAM_TOKEN_HEX + 1])
{
    unsigned char bytes[STREAM_TOKEN_BYTES];
    static const char hex[] = "0123456789abcdef";
    if (!papo_random_bytes(bytes, sizeof(bytes)))
        return false;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[STREAM_TOKEN_HEX] = 0;
    return true;
}

static bool write_all(GOutputStream *out, const void *data, size_t length)
{
    gsize written = 0;
    return length == 0 || (g_output_stream_write_all(out, data, length, &written, NULL, NULL) &&
                           written == length);
}

static void close_connection(stream_connection *connection)
{
    GSocket *socket = g_socket_connection_get_socket(connection->connection);
    if (socket)
        g_socket_shutdown(socket, TRUE, TRUE, NULL);
}

/* Must be called with the server lock held. */
static void close_token(media_stream_server *server, const char *token)
{
    for (guint i = 0; i < server->connections->len; ++i) {
        stream_connection *connection = g_ptr_array_index(server->connections, i);
        if (!token || !strcmp(connection->token, token))
            close_connection(connection);
    }
}

static void request_headers(const char *request, bool *range, size_t *start)
{
    *range = false;
    *start = 0;
    const char *body = strstr(request, "\r\n\r\n");
    if (!body)
        return;
    const char *line = strstr(request, "\r\n");
    while (line && line < body) {
        line += 2;
        const char *end = strstr(line, "\r\n");
        if (!end || end > body)
            break;
        if (end - line >= 6 && !g_ascii_strncasecmp(line, "Range:", 6)) {
            const char *value = line + 6;
            while (*value == ' ')
                ++value;
            if (end - value >= 6 && !g_ascii_strncasecmp(value, "bytes=", 6)) {
                const char *number = value + 6;
                char *next = NULL;
                guint64 offset = g_ascii_strtoull(number, &next, 10);
                /* Only the open-ended form "bytes=N-" is honored here; a
                 * bounded range needs a bounded Content-Range response. */
                if (next && next != number && next < end && *next == '-') {
                    const char *tail = next + 1;
                    while (tail < end && (*tail == ' ' || *tail == '\t'))
                        ++tail;
                    if (tail == end) {
                        *range = true;
                        *start = (size_t)offset;
                    }
                }
            }
        }
        line = end;
    }
}

static bool request_token(const char *request, char token[STREAM_TOKEN_HEX + 1])
{
    if (strncmp(request, "GET /", 5))
        return false;
    const char *value = request + 5;
    for (size_t i = 0; i < STREAM_TOKEN_HEX; ++i)
        if (!g_ascii_isxdigit(value[i]))
            return false;
    if (strncmp(value + STREAM_TOKEN_HEX, " HTTP/", 6))
        return false;
    memcpy(token, value, STREAM_TOKEN_HEX);
    token[STREAM_TOKEN_HEX] = 0;
    for (size_t i = 0; i < STREAM_TOKEN_HEX; ++i)
        token[i] = g_ascii_tolower(token[i]);
    return true;
}

static void respond_error(GOutputStream *out, const char *status, const char *extra)
{
    char response[512];
    int length = g_snprintf(response, sizeof(response),
                            "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n%s\r\n",
                            status, extra ? extra : "");
    if (length > 0)
        write_all(out, response, (size_t)MIN((size_t)length, sizeof(response) - 1));
}

static uint32_t box_size32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static uint64_t box_size64(const unsigned char *data)
{
    return ((uint64_t)box_size32(data) << 32) | box_size32(data + 4);
}

/* True when the player can consume the file sequentially from the start:
 * non-ISO-BMFF media and MP4 files whose `moov` box precedes `mdat`. Such a
 * file must not advertise ranges while it grows, because FFmpeg then probes
 * the end of the MP4 before rendering. Returns false for MP4 files whose
 * index sits after the media data; those keep range support and wait for the
 * tail, since they cannot start before parsing it. */
static bool sequential_start(FILE *file, size_t available)
{
    unsigned char head[64];
    size_t got = MIN(available, sizeof(head));
    if (got < 8 || fseek(file, 0, SEEK_SET) != 0 || fread(head, 1, got, file) != got)
        return true; /* Unknown layout: prefer early playback. */
    if (memcmp(head + 4, "ftyp", 4))
        return true; /* Not an ISO BMFF/MP4 file. */
    size_t offset = 0;
    while (offset + 8 <= got) {
        uint64_t size = box_size32(head + offset);
        const unsigned char *type = head + offset + 4;
        size_t header = 8;
        if (size == 1) {
            if (offset + 16 > got)
                return true;
            size = box_size64(head + offset + 8);
            header = 16;
        } else if (size == 0) {
            return true; /* The box runs to EOF; there is no later index. */
        }
        if (!memcmp(type, "moov", 4))
            return true;
        if (!memcmp(type, "mdat", 4))
            return false;
        if (size < header || size > SIZE_MAX - offset)
            return true;
        offset += (size_t)size;
    }
    return true; /* Not enough box headers yet: prefer early playback. */
}

static void serve_connection(stream_connection *connection)
{
    media_stream_server *server = connection->server;
    GSocket *socket = g_socket_connection_get_socket(connection->connection);
    GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(connection->connection));
    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(connection->connection));
    char request[STREAM_REQUEST_LIMIT];
    size_t used = 0;
    g_socket_set_timeout(socket, 10);
    while (used + 1 < sizeof(request)) {
        GError *error = NULL;
        gssize read =
            g_input_stream_read(in, request + used, sizeof(request) - 1 - used, NULL, &error);
        if (read <= 0) {
            g_clear_error(&error);
            return;
        }
        used += (size_t)read;
        request[used] = 0;
        if (strstr(request, "\r\n\r\n"))
            break;
    }
    if (!strstr(request, "\r\n\r\n") || !request_token(request, connection->token)) {
        respond_error(out, "400 Bad Request", NULL);
        return;
    }
    bool range = false;
    size_t start = 0;
    request_headers(request, &range, &start);
    g_mutex_lock(&server->lock);
    media_stream *stream = g_hash_table_lookup(server->tokens, connection->token);
    if (stream && !stream->withdrawn)
        stream->refs++;
    else
        stream = NULL;
    g_mutex_unlock(&server->lock);
    if (!stream) {
        respond_error(out, "404 Not Found", NULL);
        return;
    }
    FILE *file = g_fopen(stream->path, "rb");
    if (!file) {
        respond_error(out, "404 Not Found", NULL);
        stream_release(stream);
        return;
    }
    size_t total = 0, available = 0;
    bool complete = false;
    g_mutex_lock(&server->lock);
    total = stream->total;
    available = stream->available;
    complete = stream->complete;
    g_mutex_unlock(&server->lock);
    bool known = total > 0;
    bool sequential = true;
    if (known && !complete) {
        /* Wait for the first box headers before choosing the range policy; a
         * player needs them anyway, and cancellation wakes the wait. */
        while (available < 64 && !complete) {
            g_mutex_lock(&server->lock);
            for (;;) {
                if (stream->withdrawn || server->stopping)
                    break;
                if (stream->available >= 64 || stream->complete)
                    break;
                gint64 deadline = g_get_monotonic_time() + STREAM_WAIT_MS * G_TIME_SPAN_MILLISECOND;
                g_cond_wait_until(&server->wake, &server->lock, deadline);
            }
            bool stop = stream->withdrawn || server->stopping;
            available = stream->available;
            complete = stream->complete;
            g_mutex_unlock(&server->lock);
            if (stop) {
                fclose(file);
                stream_release(stream);
                return;
            }
        }
        if (!complete)
            sequential = sequential_start(file, available);
    }
    /* MP4 files whose index follows the media data keep range support and wait
     * for the tail they need. Everything else is served sequentially while it
     * grows, so an automatic tail probe cannot stall playback. Once complete,
     * every file can serve ranges. */
    bool ranges = known && (complete || !sequential);
    bool partial = range && ranges;
    if (partial && start >= total) {
        char extra[64];
        g_snprintf(extra, sizeof(extra), "Content-Range: bytes */%zu\r\n", total);
        respond_error(out, "416 Range Not Satisfiable", extra);
        fclose(file);
        stream_release(stream);
        return;
    }
    if (!partial)
        start = 0;
    GString *heads = g_string_new(NULL);
    g_string_append_printf(heads, "HTTP/1.1 %s\r\n", partial ? "206 Partial Content" : "200 OK");
    g_string_append(heads, "Content-Type: application/octet-stream\r\n");
    g_string_append(heads, "Cache-Control: no-store\r\n");
    g_string_append(heads, "Connection: close\r\n");
    if (known) {
        if (ranges)
            g_string_append(heads, "Accept-Ranges: bytes\r\n");
        g_string_append_printf(heads, "Content-Length: %zu\r\n", total - start);
        if (partial)
            g_string_append_printf(heads, "Content-Range: bytes %zu-%zu/%zu\r\n", start, total - 1,
                                   total);
    } else {
        g_string_append(heads, "Transfer-Encoding: chunked\r\n");
    }
    g_string_append(heads, "\r\n");
    bool wrote = write_all(out, heads->str, heads->len);
    g_string_free(heads, TRUE);
    g_socket_set_timeout(socket, 0);
    char buffer[STREAM_CHUNK];
    size_t offset = start;
    bool broken = !wrote, chunked = !known;
    while (!broken) {
        g_mutex_lock(&server->lock);
        for (;;) {
            if (stream->withdrawn || server->stopping)
                break;
            if (offset < stream->available || stream->complete)
                break;
            gint64 deadline = g_get_monotonic_time() + STREAM_WAIT_MS * G_TIME_SPAN_MILLISECOND;
            g_cond_wait_until(&server->wake, &server->lock, deadline);
        }
        bool stop = stream->withdrawn || server->stopping;
        bool complete = stream->complete;
        size_t available = stream->available;
        g_mutex_unlock(&server->lock);
        if (stop)
            break;
        while (offset < available) {
            size_t want = MIN(available - offset, sizeof(buffer));
            if (fseek(file, (long)offset, SEEK_SET) != 0 || fread(buffer, 1, want, file) != want) {
                broken = true;
                break;
            }
            if (chunked) {
                char header[32];
                int length = g_snprintf(header, sizeof(header), "%zx\r\n", want);
                if (!write_all(out, header, (size_t)length) || !write_all(out, buffer, want) ||
                    !write_all(out, "\r\n", 2)) {
                    broken = true;
                    break;
                }
            } else if (!write_all(out, buffer, want)) {
                broken = true;
                break;
            }
            offset += want;
        }
        if (broken)
            break;
        if (complete && offset >= available)
            break;
    }
    if (!broken && chunked)
        write_all(out, "0\r\n\r\n", 5);
    fclose(file);
    stream_release(stream);
}

static gpointer connection_thread(gpointer data)
{
    stream_connection *connection = data;
    media_stream_server *server = connection->server;
    serve_connection(connection);
    g_mutex_lock(&server->lock);
    g_ptr_array_remove(server->connections, connection);
    g_mutex_unlock(&server->lock);
    g_io_stream_close(G_IO_STREAM(connection->connection), NULL, NULL);
    g_object_unref(connection->connection);
    server_unref(server);
    g_free(connection);
    return NULL;
}

static gpointer accept_thread(gpointer data)
{
    media_stream_server *server = data;
    for (;;) {
        GError *error = NULL;
        GSocketConnection *connection =
            g_socket_listener_accept(server->listener, NULL, NULL, &error);
        if (!connection) {
            g_clear_error(&error);
            break;
        }
        g_mutex_lock(&server->lock);
        bool stopping = server->stopping;
        g_mutex_unlock(&server->lock);
        if (stopping) {
            g_object_unref(connection);
            break;
        }
        stream_connection *entry = g_new0(stream_connection, 1);
        if (!entry) {
            g_object_unref(connection);
            continue;
        }
        entry->server = server;
        entry->connection = connection;
        g_atomic_int_inc(&server->refs);
        g_mutex_lock(&server->lock);
        g_ptr_array_add(server->connections, entry);
        g_mutex_unlock(&server->lock);
        GThread *thread =
            g_thread_try_new("papo-media-stream-conn", connection_thread, entry, NULL);
        if (!thread) {
            g_mutex_lock(&server->lock);
            g_ptr_array_remove(server->connections, entry);
            g_mutex_unlock(&server->lock);
            g_object_unref(connection);
            server_unref(server);
            g_free(entry);
            continue;
        }
        g_thread_unref(thread);
    }
    return NULL;
}

media_stream_server *media_stream_server_create(void)
{
    media_stream_server *server = g_new0(media_stream_server, 1);
    if (!server)
        return NULL;
    g_mutex_init(&server->lock);
    g_cond_init(&server->wake);
    server->streams =
        g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)stream_release);
    server->tokens = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    server->connections = g_ptr_array_new();
    server->refs = 1;
    if (!server->streams || !server->tokens || !server->connections)
        goto fail;
    GError *error = NULL;
    server->socket =
        g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    GInetAddress *loopback = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    GSocketAddress *address = loopback ? g_inet_socket_address_new(loopback, 0) : NULL;
    if (loopback)
        g_object_unref(loopback);
    if (!server->socket || !address)
        goto fail_clear;
    bool bound = g_socket_bind(server->socket, address, TRUE, &error) &&
                 g_socket_listen(server->socket, &error);
    g_object_unref(address);
    if (!bound)
        goto fail_clear;
    GSocketAddress *local = g_socket_get_local_address(server->socket, &error);
    if (!local || !G_IS_INET_SOCKET_ADDRESS(local)) {
        if (local)
            g_object_unref(local);
        goto fail_clear;
    }
    server->port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local));
    g_object_unref(local);
    if (!server->port)
        goto fail_clear;
    server->listener = g_socket_listener_new();
    if (!server->listener ||
        !g_socket_listener_add_socket(server->listener, server->socket, NULL, &error))
        goto fail_clear;
    server->thread = g_thread_try_new("papo-media-stream", accept_thread, server, &error);
    if (!server->thread)
        goto fail_clear;
    g_clear_error(&error);
    return server;
fail_clear:
    g_clear_error(&error);
fail:
    media_stream_server_destroy(server);
    return NULL;
}

static void server_free(media_stream_server *server)
{
    if (server->connections)
        g_ptr_array_free(server->connections, TRUE);
    if (server->tokens)
        g_hash_table_destroy(server->tokens);
    if (server->streams)
        g_hash_table_destroy(server->streams);
    if (server->listener)
        g_object_unref(server->listener);
    if (server->socket)
        g_object_unref(server->socket);
    g_cond_clear(&server->wake);
    g_mutex_clear(&server->lock);
    g_free(server);
}

static void server_unref(media_stream_server *server)
{
    if (server && g_atomic_int_dec_and_test(&server->refs))
        server_free(server);
}

void media_stream_server_destroy(media_stream_server *server)
{
    if (!server)
        return;
    g_mutex_lock(&server->lock);
    server->stopping = true;
    g_cond_broadcast(&server->wake);
    close_token(server, NULL);
    g_mutex_unlock(&server->lock);
    /* Closing the descriptor alone does not wake a blocking accept() on
     * Linux; shutdown makes the pending call return. */
    if (server->socket) {
        g_socket_shutdown(server->socket, TRUE, TRUE, NULL);
        g_socket_close(server->socket, NULL);
    }
    if (server->listener)
        g_socket_listener_close(server->listener);
    if (server->thread) {
        g_thread_join(server->thread);
        server->thread = NULL;
    }
    server_unref(server);
}

const char *media_stream_publish(media_stream_server *server, const char *path, size_t total)
{
    if (!server || !path || !*path)
        return NULL;
    g_mutex_lock(&server->lock);
    media_stream *stream = g_hash_table_lookup(server->streams, path);
    if (!stream) {
        stream = g_new0(media_stream, 1);
        char token[STREAM_TOKEN_HEX + 1];
        if (!stream || !stream_token(token)) {
            g_free(stream);
            g_mutex_unlock(&server->lock);
            return NULL;
        }
        stream->path = g_strdup(path);
        stream->total = total;
        stream->refs = 1;
        memcpy(stream->token, token, sizeof(token));
        stream->url = g_strdup_printf("http://127.0.0.1:%u/%s", server->port, token);
        if (!stream->path || !stream->url) {
            stream_free(stream);
            g_mutex_unlock(&server->lock);
            return NULL;
        }
        g_hash_table_replace(server->streams, stream->path, stream);
        g_hash_table_replace(server->tokens, stream->token, stream);
    } else if (total) {
        stream->total = total;
    }
    const char *url = stream->url;
    g_mutex_unlock(&server->lock);
    return url;
}

const char *media_stream_url(const media_stream_server *server, const char *path)
{
    if (!server || !path)
        return NULL;
    media_stream_server *mutable = (media_stream_server *)server;
    g_mutex_lock(&mutable->lock);
    media_stream *stream = g_hash_table_lookup(mutable->streams, path);
    const char *url = stream && !stream->withdrawn ? stream->url : NULL;
    g_mutex_unlock(&mutable->lock);
    return url;
}

void media_stream_progress(media_stream_server *server, const char *path, size_t available,
                           size_t total)
{
    if (!server || !path)
        return;
    g_mutex_lock(&server->lock);
    media_stream *stream = g_hash_table_lookup(server->streams, path);
    if (stream) {
        if (available > stream->available)
            stream->available = available;
        if (total)
            stream->total = total;
        g_cond_broadcast(&server->wake);
    }
    g_mutex_unlock(&server->lock);
}

void media_stream_finish(media_stream_server *server, const char *path)
{
    if (!server || !path)
        return;
    g_mutex_lock(&server->lock);
    media_stream *stream = g_hash_table_lookup(server->streams, path);
    if (stream) {
        stream->complete = true;
        g_cond_broadcast(&server->wake);
    }
    g_mutex_unlock(&server->lock);
}

void media_stream_withdraw(media_stream_server *server, const char *path)
{
    if (!server || !path)
        return;
    g_mutex_lock(&server->lock);
    media_stream *stream = g_hash_table_lookup(server->streams, path);
    if (stream) {
        char token[STREAM_TOKEN_HEX + 1];
        memcpy(token, stream->token, sizeof(token));
        stream->withdrawn = true;
        /* The token table keys live inside the stream, so drop that entry
         * before the streams table may release the last reference. */
        g_hash_table_remove(server->tokens, token);
        g_hash_table_remove(server->streams, path);
        close_token(server, token);
        g_cond_broadcast(&server->wake);
    }
    g_mutex_unlock(&server->lock);
}
