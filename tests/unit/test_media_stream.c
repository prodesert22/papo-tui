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

static void box(FILE *file, const char *type, size_t size)
{
    unsigned char header[8] = {(unsigned char)(size >> 24), (unsigned char)(size >> 16),
                               (unsigned char)(size >> 8), (unsigned char)size};
    memcpy(header + 4, type, 4);
    assert(fwrite(header, 1, sizeof(header), file) == sizeof(header));
}

static void fill(FILE *file, size_t count, char value)
{
    char block[256];
    memset(block, value, sizeof(block));
    while (count) {
        size_t piece = count < sizeof(block) ? count : sizeof(block);
        assert(fwrite(block, 1, piece, file) == piece);
        count -= piece;
    }
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

    /* The body starts before the transfer ends and is delivered sequentially.
     * No ranges are advertised while the file grows, otherwise FFmpeg seeks
     * to the end of the MP4 and waits for the whole download. */
    GSocketConnection *connection = connect_url(url);
    assert(connection);
    GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, token, "bytes=0-");
    char *head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 200 OK"));
    assert(strstr(head, "Content-Length: 3000"));
    assert(!strstr(head, "Accept-Ranges"));
    g_free(head);
    char received[3000];
    assert(read_exact(in, received, sizeof(first)));
    assert(!memcmp(received, first, sizeof(first)));

    /* A tail request before the bytes exist must not wait for the rest of the
     * transfer: it is answered as a sequential body from the start. */
    GSocketConnection *probe = connect_url(url);
    assert(probe);
    GInputStream *probe_in = g_io_stream_get_input_stream(G_IO_STREAM(probe));
    request(probe, token, "bytes=2000-");
    char *probe_head = read_headers(probe_in);
    assert(strstr(probe_head, "HTTP/1.1 200 OK"));
    assert(strstr(probe_head, "Content-Length: 3000"));
    assert(!strstr(probe_head, "Accept-Ranges"));
    assert(!strstr(probe_head, "Content-Range"));
    g_free(probe_head);
    close_connection(probe);

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

    /* MP4 with the index after the media data keeps ranges while it grows:
     * it cannot render before the tail arrives, so the player waits for it. */
    FILE *nonfast = NULL;
    char *nonfast_path = temp_file(&nonfast);
    box(nonfast, "ftyp", 24);
    fill(nonfast, 16, 'f');
    box(nonfast, "mdat", 3000);
    fill(nonfast, 2000, 'm');
    assert(fflush(nonfast) == 0);
    const char *nonfast_url = media_stream_publish(server, nonfast_path, 3124);
    assert(nonfast_url);
    const char *nonfast_token = strrchr(nonfast_url, '/') + 1;
    media_stream_progress(server, nonfast_path, 2032, 3124);
    connection = connect_url(nonfast_url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, nonfast_token, "bytes=0-");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 206 Partial Content"));
    assert(strstr(head, "Accept-Ranges: bytes"));
    assert(strstr(head, "Content-Length: 3124"));
    assert(strstr(head, "Content-Range: bytes 0-3123/3124"));
    g_free(head);
    close_connection(connection);

    connection = connect_url(nonfast_url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, nonfast_token, "bytes=3024-");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 206 Partial Content"));
    assert(strstr(head, "Content-Range: bytes 3024-3123/3124"));
    assert(strstr(head, "Content-Length: 100"));
    g_free(head);
    /* Complete the mdat and append the moov box; the pending tail request
     * wakes and receives the index. */
    fill(nonfast, 992, 'm');
    box(nonfast, "moov", 100);
    fill(nonfast, 92, 'o');
    assert(fflush(nonfast) == 0);
    media_stream_progress(server, nonfast_path, 3124, 3124);
    media_stream_finish(server, nonfast_path);
    char moov[100];
    assert(read_exact(in, moov, sizeof(moov)));
    assert(!memcmp(moov + 4, "moov", 4));
    close_connection(connection);

    /* Fast-start MP4 (moov before mdat) is served sequentially while it grows,
     * so a tail probe cannot block the first frame. */
    FILE *faststart = NULL;
    char *faststart_path = temp_file(&faststart);
    box(faststart, "ftyp", 24);
    fill(faststart, 16, 'f');
    box(faststart, "moov", 100);
    fill(faststart, 92, 'o');
    box(faststart, "mdat", 3000);
    fill(faststart, 2000, 'm');
    assert(fflush(faststart) == 0);
    const char *faststart_url = media_stream_publish(server, faststart_path, 3124);
    assert(faststart_url);
    const char *faststart_token = strrchr(faststart_url, '/') + 1;
    media_stream_progress(server, faststart_path, 2132, 3124);
    connection = connect_url(faststart_url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, faststart_token, "bytes=0-");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 200 OK"));
    assert(!strstr(head, "Accept-Ranges"));
    assert(strstr(head, "Content-Length: 3124"));
    g_free(head);
    close_connection(connection);

    fill(faststart, 992, 'm');
    assert(fflush(faststart) == 0);
    media_stream_progress(server, faststart_path, 3124, 3124);
    media_stream_finish(server, faststart_path);
    connection = connect_url(faststart_url);
    assert(connection);
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request(connection, faststart_token, "bytes=1000-");
    head = read_headers(in);
    assert(strstr(head, "HTTP/1.1 206 Partial Content"));
    assert(strstr(head, "Accept-Ranges: bytes"));
    assert(strstr(head, "Content-Range: bytes 1000-3123/3124"));
    g_free(head);
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
    media_stream_withdraw(server, nonfast_path);
    media_stream_withdraw(server, faststart_path);

    media_stream_server_destroy(server);
    fclose(file);
    fclose(chunked_file);
    fclose(nonfast);
    fclose(faststart);
    g_unlink(path);
    g_unlink(chunked_path);
    g_unlink(nonfast_path);
    g_unlink(faststart_path);
    g_free(path);
    g_free(chunked_path);
    g_free(nonfast_path);
    g_free(faststart_path);
    return 0;
}
