#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/clipboard.h"
#include "net/clock.h"
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static struct {
    char data[TEXT_UTF8_BYTES];
    size_t length, sent;
    pid_t child;
    int socket;
    uint64_t started, until, revision;
    const char *notice;
} copy = {.socket = -1};
static void wipe(void)
{
    volatile unsigned char *bytes = (volatile unsigned char *)copy.data;
    for (size_t i = 0; i < sizeof(copy.data); ++i)
        bytes[i] = 0;
    copy.length = copy.sent = 0;
}
static void feedback(const char *text)
{
    uint64_t now = 0;
    net_now_ms(&now);
    copy.notice = text;
    copy.until = now + 3000;
    ++copy.revision;
}
static bool terminal_copy(void)
{
    char encoded[((TEXT_UTF8_BYTES + 2) / 3) * 4 + 1];
    int state = 0, save = 0;
    size_t length =
        g_base64_encode_step((const guchar *)copy.data, copy.length, false, encoded, &state, &save);
    length += g_base64_encode_close(false, encoded + length, &state, &save);
    encoded[length] = 0;
    bool ok = fprintf(stdout, "\033]52;c;%s\a", encoded) >= 0 && fflush(stdout) == 0;
    volatile char *p = encoded;
    for (size_t i = 0; i < sizeof(encoded); ++i)
        p[i] = 0;
    wipe();
    feedback(ok ? "Texto copiado" : "Falha ao copiar texto");
    return ok;
}
static bool native_copy(char *const arguments[])
{
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair))
        return false;
    fcntl(pair[0], F_SETFD, FD_CLOEXEC);
    fcntl(pair[1], F_SETFD, FD_CLOEXEC);
    if (fcntl(pair[0], F_SETFL, O_NONBLOCK) == -1) {
        close(pair[0]);
        close(pair[1]);
        return false;
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions)) {
        close(pair[0]);
        close(pair[1]);
        return false;
    }
    int error = posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
    if (!error)
        error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (!error)
        error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (!error)
        error = posix_spawnp(&copy.child, arguments[0], &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (error) {
        close(pair[0]);
        copy.child = 0;
        return false;
    }
    copy.socket = pair[0];
    net_now_ms(&copy.started);
    copy.notice = NULL;
    return true;
}
bool clipboard_copy(const text_buffer *buffer)
{
    size_t lo, hi;
    if (!text_selection(buffer, &lo, &hi))
        return false;
    if (copy.child) {
        feedback("Cópia em andamento");
        return false;
    }
    text_buffer selected = {0}, visible;
    selected.length = hi - lo;
    memcpy(selected.data, buffer->data + lo, selected.length * sizeof(selected.data[0]));
    selected.mentions = buffer->mentions;
    selected.resolver = buffer->resolver;
    selected.resolver_context = buffer->resolver_context;
    text_project(&selected, &visible);
    bool ok = text_encode(&visible, copy.data);
    volatile unsigned char *p = (volatile unsigned char *)&selected;
    for (size_t i = 0; i < sizeof(selected); ++i)
        p[i] = 0;
    p = (volatile unsigned char *)&visible;
    for (size_t i = 0; i < sizeof(visible); ++i)
        p[i] = 0;
    if (!ok) {
        wipe();
        feedback("Falha ao copiar texto");
        return false;
    }
    copy.length = strlen(copy.data);
    copy.sent = 0;
    char *wayland[] = {"wl-copy", "--type", "text/plain;charset=utf-8", NULL};
    char *x11[] = {"xclip", "-selection", "clipboard", "-in", NULL};
    if ((getenv("WAYLAND_DISPLAY") && *getenv("WAYLAND_DISPLAY") && native_copy(wayland)) ||
        (getenv("DISPLAY") && *getenv("DISPLAY") && native_copy(x11)))
        return true;
    return terminal_copy();
}
bool clipboard_poll(void)
{
    uint64_t now = 0;
    net_now_ms(&now);
    bool changed = false;
    if (copy.child) {
        if (copy.socket >= 0) {
            ssize_t sent =
                send(copy.socket, copy.data + copy.sent, copy.length - copy.sent, MSG_NOSIGNAL);
            if (sent > 0)
                copy.sent += (size_t)sent;
            if (copy.sent == copy.length ||
                (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                close(copy.socket);
                copy.socket = -1;
            }
        }
        int status;
        pid_t result = waitpid(copy.child, &status, WNOHANG);
        if (result == copy.child || (result < 0 && errno == ECHILD)) {
            copy.child = 0;
            if (copy.socket >= 0) {
                close(copy.socket);
                copy.socket = -1;
            }
            if (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
                copy.sent == copy.length) {
                wipe();
                feedback("Texto copiado");
            } else
                terminal_copy();
            changed = true;
        } else if (now - copy.started >= 2000) {
            kill(copy.child, SIGKILL); /* Reap without blocking on the next poll. */
        }
    }
    if (copy.notice && now >= copy.until) {
        copy.notice = NULL;
        changed = true;
    }
    return changed;
}
const char *clipboard_notice(void)
{
    return copy.notice;
}
uint64_t clipboard_revision(void)
{
    return copy.revision;
}
void clipboard_shutdown(void)
{
    if (copy.socket >= 0) {
        close(copy.socket);
        copy.socket = -1;
    }
    if (copy.child) {
        kill(copy.child, SIGKILL);
        while (waitpid(copy.child, NULL, 0) < 0 && errno == EINTR) {
        }
        copy.child = 0;
    }
    wipe();
    copy.notice = NULL;
}
