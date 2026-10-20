#include "ui/media/media_stream.h"
#include <assert.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_exact(GInputStream *in, char *buffer, size_t size)
{
    size_t used = 0;
    while (used < size) {
        gsize read = 0;
        GError *error = NULL;
        if (!g_input_stream_read_all(in, buffer + used, size - used, &read, NULL, &error) ||
            !read) {
            g_clear_error(&error);
            return false;
        }
        used += read;
    }
    return true;
}

static char *read_line(GInputStream *in)
{
    GString *line = g_string_new(NULL);
    for (;;) {
        char byte = 0;
        gsize read = 0;
        GError *error = NULL;
        if (!g_input_stream_read_all(in, &byte, 1, &read, NULL, &error) || read != 1) {
            g_clear_error(&error);
            break;
        }
        if (byte == '\n')
            break;
        if (byte != '\r')
            g_string_append_c(line, byte);
    }
    return g_string_free(line, FALSE);
}

static char *read_headers(GInputStream *in)
{
    GString *head = g_string_new(NULL);
    while (head->len < 8192) {
        char byte = 0;
        gsize read = 0;
        GError *error = NULL;
        if (!g_input_stream_read_all(in, &byte, 1, &read, NULL, &error) || read != 1) {
            g_clear_error(&error);
            break;
        }
        g_string_append_c(head, byte);
        if (g_str_has_suffix(head->str, "\r\n\r\n"))
            break;
    }
    return g_string_free(head, FALSE);
}

static GSocketConnection *connect_url(const char *url)
{
    static const char prefix[] = "http://";
    assert(g_str_has_prefix(url, prefix));
    const char *host = url + strlen(prefix);
    const char *colon = strchr(host, ':');
    assert(colon && colon[1]);
    char *name = g_strndup(host, (gsize)(colon - host));
    unsigned port = (unsigned)atoi(colon + 1);
    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_timeout(client, 10);
    GError *error = NULL;
    GSocketConnection *connection =
        g_socket_client_connect_to_host(client, name, (guint16)port, NULL, &error);
    g_clear_error(&error);
    g_object_unref(client);
    g_free(name);
    return connection;
}

static void request(GSocketConnection *connection, const char *token, const char *range)
{
    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    char head[512];
    int length =
        range
            ? g_snprintf(head, sizeof(head),
                         "GET /%s HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: %s\r\n\r\n", token, range)
            : g_snprintf(head, sizeof(head), "GET /%s HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", token);
    gsize written = 0;
    assert(g_output_stream_write_all(out, head, (gsize)length, &written, NULL, NULL));
    assert(written == (gsize)length);
}

static void close_connection(GSocketConnection *connection)
{
    g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
    g_object_unref(connection);
}

static char *temp_file(FILE **file)
{
    GError *error = NULL;
    char *path = NULL;
    int fd = g_file_open_tmp("papo-stream-test-XXXXXX", &path, &error);
    assert(fd >= 0);
    assert(g_close(fd, NULL));
    *file = g_fopen(path, "wb");
    assert(*file);
    return path;
}

int main(void)
{
    FILE *file = NULL;
    char *path = temp_file(&file);
    media_stream_server *server = media_stream_server_create();
    assert(server);
    char first[1000], second[1000], third[1000];
    for (size_t i = 0; i < sizeof(first); ++i) {
        first[i] = (char)('a' + i % 26);
        second[i] = (char)('A' + i % 26);
        third[i] = (char)('0' + i % 10);
    }
    assert(fwrite(first, 1, sizeof(first), file) == sizeof(first));
    assert(fflush(file) == 0);
    const char *url = media_stream_publish(server, path, 3000);
    assert(url && g_str_has_prefix(url, "http://127.0.0.1:"));
    const char *token = strrchr(url, '/') + 1;
    assert(strlen(token) == 32);
    assert(media_stream_url(server, path) == url);
    media_stream_progress(server, path, sizeof(first), 3000);

    /* The response starts before the transfer ends and keeps the announced
     * length, so the player can seek inside the downloaded part. */
    GSocketConnection *connection = connect_url(url);
    assert(connection);
    GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, token, NULL);
    char *head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 200 OK"));
    assert(strstr(head, "Content-Length: 3000"));
    assert(strstr(head, "Accept-Ranges: bytes"));
    g_free(head);
    char received[3000];
    assert(read_exact(in, received, sizeof(first)));
    assert(!memcmp(received, first, sizeof(first)));

    assert(fwrite(second, 1, sizeof(second), file) == sizeof(second));
    assert(fflush(file) == 0);
    media_stream_progress(server, path, sizeof(first) + sizeof(second), 3000);
    assert(read_exact(in, received + sizeof(first), sizeof(second)));
    assert(!memcmp(received + sizeof(first), second, sizeof(second)));

    assert(fwrite(third, 1, sizeof(third), file) == sizeof(third));
    assert(fflush(file) == 0);
    media_stream_progress(server, path, 3000, 3000);
    media_stream_finish(server, path);
    assert(read_exact(in, received + 2000, sizeof(third)));
    assert(!memcmp(received + 2000, third, sizeof(third)));
    gsize extra = 0;
    char byte = 0;
    g_input_stream_read_all(in, &byte, 1, &extra, NULL, NULL);
    assert(extra == 0);
    close_connection(connection);

    /* A later range request is served from the same published file. */
    connection = connect_url(url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, token, "bytes=1000-");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 206 Partial Content"));
    assert(strstr(head, "Content-Range: bytes 1000-2999/3000"));
    assert(strstr(head, "Content-Length: 2000"));
    g_free(head);
    char ranged[2000];
    assert(read_exact(in, ranged, sizeof(ranged)));
    assert(!memcmp(ranged, second, sizeof(second)) && !memcmp(ranged + 1000, third, sizeof(third)));
    close_connection(connection);

    /* A start beyond the announced length is unsatisfiable... */
    connection = connect_url(url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, token, "bytes=3000-");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 416 Range Not Satisfiable"));
    assert(strstr(head, "Content-Range: bytes */3000"));
    g_free(head);
    close_connection(connection);

    /* ...and a bounded range is ignored instead of being answered with more
     * bytes than requested. */
    connection = connect_url(url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, token, "bytes=0-99");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 200 OK"));
    assert(strstr(head, "Content-Length: 3000"));
    g_free(head);
    close_connection(connection);

    /* Unknown length falls back to chunked delivery. */
    FILE *chunked_file = NULL;
    char *chunked_path = temp_file(&chunked_file);
    const char *chunked_url = media_stream_publish(server, chunked_path, 0);
    assert(chunked_url);
    const char *chunked_token = strrchr(chunked_url, '/') + 1;
    char chunked[700];
    memset(chunked, 'z', sizeof(chunked));
    assert(fwrite(chunked, 1, sizeof(chunked), chunked_file) == sizeof(chunked));
    assert(fflush(chunked_file) == 0);
    media_stream_progress(server, chunked_path, sizeof(chunked), 0);
    connection = connect_url(chunked_url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, chunked_token, NULL);
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 200 OK"));
    assert(strstr(head, "Transfer-Encoding: chunked"));
    /* Without a known length the bridge cannot honor ranges, and advertising
     * them makes FFmpeg seek forward until the whole body arrives. */
    assert(!strstr(head, "Accept-Ranges"));
    g_free(head);
    char *line = read_line(in);
    assert(!strcmp(line, "2bc"));
    g_free(line);
    char body[700];
    assert(read_exact(in, body, sizeof(body)));
    assert(!memcmp(body, chunked, sizeof(body)));
    line = read_line(in);
    assert(!*line);
    g_free(line);
    media_stream_finish(server, chunked_path);
    line = read_line(in);
    assert(!strcmp(line, "0"));
    g_free(line);
    char end[2];
    assert(read_exact(in, end, sizeof(end)));
    assert(!memcmp(end, "\r\n", 2));
    close_connection(connection);

    /* Withdrawing closes the token: an old URL must not expose the bytes. */
    char *old_url = g_strdup(url);
    char old_token[33];
    snprintf(old_token, sizeof(old_token), "%s", token);
    media_stream_withdraw(server, path);
    assert(!media_stream_url(server, path));
    connection = connect_url(old_url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, old_token, NULL);
    head = read_headers(in);
    assert(strstr(head, "404 Not Found"));
    g_free(head);
    close_connection(connection);
    g_free(old_url);
    media_stream_withdraw(server, chunked_path);

    media_stream_server_destroy(server);
    fclose(file);
    fclose(chunked_file);
    g_unlink(path);
    g_unlink(chunked_path);
    g_free(path);
    g_free(chunked_path);
    return 0;
}
