#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/linux/terminal_focus.h"
#include <fcntl.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef HAVE_XCB
#include <xcb/xcb.h>
#endif

#define ANCESTOR_LIMIT 64
#define REPLY_LIMIT (16u << 20)

/* Environment is copied on the caller's thread; the worker never calls getenv. */
struct request {
    GDBusConnection *bus;
    char title[1025];
    bool tmux;
    char tmux_pane[16], konsole_service[256], konsole_window[256], konsole_session[64];
    char wezterm_pane[16], wezterm_dir[1024], desktop[256], sway[108], hyprland[128], runtime[1024],
        display[256], window[32];
};
static void copy_env(char *out, size_t capacity, const char *name)
{
    const char *value = getenv(name);
    if (!value || strlen(value) >= capacity)
        value = "";
    memcpy(out, value, strlen(value) + 1);
}
static bool digits(const char *text)
{
    size_t length = strlen(text);
    if (!length || length > 9)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (text[i] < '0' || text[i] > '9')
            return false;
    return true;
}

/* Nearest first: the process itself, its parent, and so on below init. */
static size_t ancestors(long start, long *pids, size_t capacity)
{
    size_t count = 0;
    for (long pid = start; pid > 1 && count < capacity;) {
        pids[count++] = pid;
        char path[64], line[1024];
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        FILE *file = fopen(path, "re");
        if (!file)
            break;
        size_t size = fread(line, 1, sizeof(line) - 1, file);
        fclose(file);
        line[size] = 0;
        /* The command name may itself contain ')' or spaces. */
        const char *name_end = strrchr(line, ')');
        long parent;
        if (!name_end || sscanf(name_end + 1, " %*c %ld", &parent) != 1 || parent == pid)
            break;
        pid = parent;
    }
    return count;
}
static size_t rank(const long *pids, size_t count, long pid)
{
    for (size_t i = 0; i < count; ++i)
        if (pids[i] == pid)
            return i;
    return SIZE_MAX;
}
/* Lower is better: the nearest ancestor wins, then a window whose title holds
 * ours, for terminals that keep several windows in one process. */
static size_t score(const long *pids, size_t count, long pid, const char *caption,
                    const char *title)
{
    size_t position = rank(pids, count, pid);
    if (position == SIZE_MAX)
        return SIZE_MAX;
    return position * 2 + (*title && caption && strstr(caption, title) ? 0 : 1);
}

/* Runs a short helper without the terminal: stdin is closed, stderr dropped. */
static bool run(const char *const *argv, char **out)
{
    GError *error = NULL;
    GSubprocess *process = g_subprocess_newv(
        argv,
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE |
            (out ? G_SUBPROCESS_FLAGS_STDOUT_PIPE : G_SUBPROCESS_FLAGS_STDOUT_SILENCE),
        &error);
    if (!process) {
        g_clear_error(&error);
        return false;
    }
    bool ok = g_subprocess_communicate_utf8(process, NULL, NULL, out, NULL, &error) &&
              g_subprocess_get_if_exited(process) && !g_subprocess_get_exit_status(process);
    g_clear_error(&error);
    g_object_unref(process);
    return ok;
}

/* Selects this pane in tmux and returns the pid of the client most recently
 * showing it, whose ancestors own the window; -1 when no client shows it and
 * 0 outside tmux. */
static long select_tmux(const struct request *r)
{
    const char *pane = r->tmux_pane;
    if (!r->tmux || pane[0] != '%' || !digits(pane + 1))
        return 0;
    const char *select[] = {"tmux", "select-window", "-t", pane, ";", "select-pane", "-t", pane,
                            ";",    "switch-client", "-t", pane, NULL};
    run(select, NULL);
    const char *clients[] = {
        "tmux", "list-clients", "-t", pane, "-F", "#{client_activity} #{client_pid}", NULL};
    char *out = NULL;
    long best = -1;
    unsigned long long newest = 0;
    if (run(clients, &out) && out)
        for (char *save = NULL, *line = strtok_r(out, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            unsigned long long activity;
            long pid;
            if (sscanf(line, "%llu %ld", &activity, &pid) == 2 && pid > 1 &&
                (best < 0 || activity > newest)) {
                best = pid;
                newest = activity;
            }
        }
    g_free(out);
    return best;
}
static bool select_konsole(const struct request *r)
{
    const char *session = r->konsole_session;
    if (!r->bus || !g_dbus_is_name(r->konsole_service) ||
        !g_variant_is_object_path(r->konsole_window) || strncmp(session, "/Sessions/", 10) ||
        !digits(session + 10))
        return false;
    GVariant *reply = g_dbus_connection_call_sync(
        r->bus, r->konsole_service, r->konsole_window, "org.kde.konsole.Window",
        "setCurrentSession", g_variant_new("(i)", atoi(session + 10)), NULL,
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, NULL);
    if (reply)
        g_variant_unref(reply);
    return reply != NULL;
}
static bool select_wezterm(const struct request *r)
{
    if (!digits(r->wezterm_pane))
        return false;
    /* WEZTERM_EXECUTABLE is the GUI; the CLI sits next to it. */
    char *cli = *r->wezterm_dir ? g_build_filename(r->wezterm_dir, "wezterm", NULL) : NULL;
    const char *argv[] = {cli && g_file_test(cli, G_FILE_TEST_IS_EXECUTABLE) ? cli : "wezterm",
                          "cli",
                          "activate-pane",
                          "--pane-id",
                          r->wezterm_pane,
                          NULL};
    bool ok = run(argv, NULL);
    g_free(cli);
    return ok;
}

static bool listed(const char *list, const char *word)
{
    for (const char *at = list; *at;) {
        size_t length = strcspn(at, ":");
        if (length == strlen(word) && !strncmp(at, word, length))
            return true;
        at += length + (at[length] == ':');
    }
    return false;
}
static const char kwin_script[] =
    "(function (pids, title) {\n"
    "    const windows = workspace.windowList ? workspace.windowList() : workspace.clientList();\n"
    "    let best = null, bestScore = Infinity;\n"
    "    for (const w of windows) {\n"
    "        const rank = pids.indexOf(w.pid);\n"
    "        if (rank < 0 || !w.normalWindow)\n"
    "            continue;\n"
    "        const named = title && String(w.caption).indexOf(title) >= 0;\n"
    "        const score = rank * 2 + (named ? 0 : 1);\n"
    "        if (score < bestScore) {\n"
    "            best = w;\n"
    "            bestScore = score;\n"
    "        }\n"
    "    }\n"
    "    if (!best)\n"
    "        return;\n"
    "    best.minimized = false;\n"
    "    if (\"activeWindow\" in workspace)\n"
    "        workspace.activeWindow = best;\n"
    "    else\n"
    "        workspace.activeClient = best;\n"
    "})(%s, %s);\n";
static GVariant *kwin_call(const struct request *r, const char *path, const char *iface,
                           const char *method, GVariant *arguments)
{
    return g_dbus_connection_call_sync(r->bus, "org.kde.KWin", path, iface, method, arguments, NULL,
                                       G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, NULL);
}
/* A KWin script may activate any window, also on Wayland, and is not subject
 * to focus stealing prevention. It is loaded, run once and unloaded. */
static bool kwin(const struct request *r, const long *pids, size_t count)
{
    json_t *list = json_array(), *name = json_string(r->title);
    for (size_t i = 0; list && i < count; ++i)
        json_array_append_new(list, json_integer(pids[i]));
    char *pids_json = list ? json_dumps(list, JSON_COMPACT) : NULL;
    char *title_json = json_dumps(name ? name : json_string(""), JSON_ENCODE_ANY);
    json_decref(list);
    json_decref(name);
    gchar *script =
        pids_json && title_json ? g_strdup_printf(kwin_script, pids_json, title_json) : NULL;
    free(pids_json);
    free(title_json);
    char *path = g_build_filename(*r->runtime ? r->runtime : g_get_tmp_dir(),
                                  "papo-tui-focus-XXXXXX.js", NULL);
    int fd = script ? g_mkstemp_full(path, O_WRONLY | O_CLOEXEC, 0600) : -1;
    bool written = false;
    if (fd >= 0) {
        size_t length = strlen(script), done = 0;
        while (done < length) {
            ssize_t step = write(fd, script + done, length - done);
            if (step <= 0)
                break;
            done += (size_t)step;
        }
        written = done == length;
        close(fd);
    }
    g_free(script);
    bool ran = false;
    if (written) {
        static gint serial;
        char plugin[64];
        snprintf(plugin, sizeof(plugin), "papo-tui-focus-%ld-%d", (long)getpid(),
                 g_atomic_int_add(&serial, 1));
        GVariant *reply = kwin_call(r, "/Scripting", "org.kde.kwin.Scripting", "loadScript",
                                    g_variant_new("(ss)", path, plugin));
        int id = -1;
        if (reply && g_variant_is_of_type(reply, G_VARIANT_TYPE("(i)")))
            g_variant_get(reply, "(i)", &id);
        if (reply)
            g_variant_unref(reply);
        if (id >= 0) {
            /* Plasma 6 names the script object /Scripting/ScriptN; Plasma 5 /N. */
            char object[64];
            snprintf(object, sizeof(object), "/Scripting/Script%d", id);
            reply = kwin_call(r, object, "org.kde.kwin.Script", "run", NULL);
            if (!reply) {
                snprintf(object, sizeof(object), "/%d", id);
                reply = kwin_call(r, object, "org.kde.kwin.Script", "run", NULL);
            }
            ran = reply != NULL;
            if (reply)
                g_variant_unref(reply);
            g_usleep(500 * 1000);
            reply = kwin_call(r, "/Scripting", "org.kde.kwin.Scripting", "unloadScript",
                              g_variant_new("(s)", plugin));
            if (reply)
                g_variant_unref(reply);
        }
    }
    if (fd >= 0)
        unlink(path);
    g_free(path);
    return ran;
}

static int connect_unix(const char *path)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path))
        return -1;
    memcpy(address.sun_path, path, strlen(path) + 1);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct timeval limit = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        close(fd);
        return -1;
    }
    return fd;
}
static bool transfer(int fd, void *data, size_t length, bool writing)
{
    for (size_t done = 0; done < length;) {
        ssize_t step = writing ? send(fd, (char *)data + done, length - done, MSG_NOSIGNAL)
                               : recv(fd, (char *)data + done, length - done, 0);
        if (step <= 0)
            return false;
        done += (size_t)step;
    }
    return true;
}
/* One i3/sway IPC exchange: "i3-ipc", payload length, type, payload. */
static json_t *sway_call(int fd, uint32_t type, const char *payload)
{
    char header[14] = "i3-ipc";
    uint32_t length = (uint32_t)strlen(payload);
    memcpy(header + 6, &length, 4);
    memcpy(header + 10, &type, 4);
    if (!transfer(fd, header, sizeof(header), true) ||
        !transfer(fd, (void *)payload, length, true) ||
        !transfer(fd, header, sizeof(header), false) || memcmp(header, "i3-ipc", 6))
        return NULL;
    memcpy(&length, header + 6, 4);
    char *reply = length <= REPLY_LIMIT ? malloc(length ? length : 1) : NULL;
    json_t *value =
        reply && transfer(fd, reply, length, false) ? json_loadb(reply, length, 0, NULL) : NULL;
    free(reply);
    return value;
}
static void sway_find(const json_t *node, const long *pids, size_t count, const char *title,
                      json_int_t *best, size_t *best_score)
{
    const json_t *pid = json_object_get(node, "pid"), *id = json_object_get(node, "id");
    if (json_is_integer(pid) && json_is_integer(id)) {
        size_t value = score(pids, count, (long)json_integer_value(pid),
                             json_string_value(json_object_get(node, "name")), title);
        if (value < *best_score) {
            *best = json_integer_value(id);
            *best_score = value;
        }
    }
    const char *children[] = {"nodes", "floating_nodes"};
    for (size_t k = 0; k < 2; ++k) {
        size_t i;
        const json_t *child;
        json_array_foreach(json_object_get(node, children[k]), i, child)
            sway_find(child, pids, count, title, best, best_score);
    }
}
static bool sway(const struct request *r, const long *pids, size_t count)
{
    int fd = connect_unix(r->sway);
    if (fd < 0)
        return false;
    json_t *tree = sway_call(fd, 4, ""); /* GET_TREE */
    json_int_t best = -1;
    size_t best_score = SIZE_MAX;
    if (tree)
        sway_find(tree, pids, count, r->title, &best, &best_score);
    json_decref(tree);
    bool ok = false;
    if (best >= 0) {
        char command[64];
        snprintf(command, sizeof(command), "[con_id=%" JSON_INTEGER_FORMAT "] focus", best);
        json_t *reply = sway_call(fd, 0, command); /* RUN_COMMAND */
        ok = reply != NULL;
        json_decref(reply);
    }
    close(fd);
    return ok;
}

/* Hyprland answers one request per connection and then closes it. */
static char *hyprland_call(const struct request *r, const char *command)
{
    char path[1280];
    int fd = -1;
    if (*r->runtime) {
        snprintf(path, sizeof(path), "%s/hypr/%s/.socket.sock", r->runtime, r->hyprland);
        fd = connect_unix(path);
    }
    if (fd < 0) {
        snprintf(path, sizeof(path), "/tmp/hypr/%s/.socket.sock", r->hyprland);
        fd = connect_unix(path);
    }
    if (fd < 0)
        return NULL;
    GString *reply = NULL;
    if (transfer(fd, (void *)command, strlen(command), true)) {
        reply = g_string_new(NULL);
        char buffer[4096];
        ssize_t step;
        while (reply->len < REPLY_LIMIT && (step = recv(fd, buffer, sizeof(buffer), 0)) > 0)
            g_string_append_len(reply, buffer, step);
    }
    close(fd);
    return reply ? g_string_free(reply, FALSE) : NULL;
}
static bool hyprland(const struct request *r, const long *pids, size_t count)
{
    for (const char *at = r->hyprland; *at; ++at)
        if (!g_ascii_isalnum(*at) && *at != '_')
            return false;
    char *text = *r->hyprland ? hyprland_call(r, "j/clients") : NULL;
    json_t *clients = text ? json_loads(text, 0, NULL) : NULL;
    g_free(text);
    const char *best = NULL;
    size_t best_score = SIZE_MAX, i;
    const json_t *client;
    json_array_foreach(clients, i, client)
    {
        const json_t *pid = json_object_get(client, "pid");
        const char *address = json_string_value(json_object_get(client, "address"));
        if (!json_is_integer(pid) || !address || strncmp(address, "0x", 2) ||
            strlen(address) > 32 ||
            strspn(address + 2, "0123456789abcdefABCDEF") != strlen(address + 2))
            continue;
        size_t value = score(pids, count, (long)json_integer_value(pid),
                             json_string_value(json_object_get(client, "title")), r->title);
        if (value < best_score) {
            best = address;
            best_score = value;
        }
    }
    char command[80];
    if (best)
        snprintf(command, sizeof(command), "dispatch focuswindow address:%s", best);
    json_decref(clients);
    char *reply = best ? hyprland_call(r, command) : NULL;
    bool ok = reply != NULL;
    g_free(reply);
    return ok;
}

#ifdef HAVE_XCB
static xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t value = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return value;
}
static xcb_get_property_reply_t *property(xcb_connection_t *c, xcb_window_t window, xcb_atom_t name,
                                          xcb_atom_t type, uint32_t length)
{
    return xcb_get_property_reply(c, xcb_get_property(c, 0, window, name, type, 0, length), NULL);
}
/* Asks an EWMH window manager to activate the window, as a pager would. */
static bool x11(const struct request *r, const long *pids, size_t count, bool tmux)
{
    int number = 0;
    xcb_connection_t *c = xcb_connect(r->display, &number);
    if (xcb_connection_has_error(c)) {
        xcb_disconnect(c);
        return false;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (int i = 0; i < number && screens.rem; ++i)
        xcb_screen_next(&screens);
    xcb_window_t root = screens.rem ? screens.data->root : XCB_WINDOW_NONE, best = 0;
    xcb_atom_t clients = atom(c, "_NET_CLIENT_LIST"), pid_atom = atom(c, "_NET_WM_PID"),
               name_atom = atom(c, "_NET_WM_NAME"), active = atom(c, "_NET_ACTIVE_WINDOW");
    /* WINDOWID names the terminal window exactly, unless a multiplexer
     * server inherited it from an older terminal. */
    if (!tmux && digits(r->window))
        best = (xcb_window_t)strtoul(r->window, NULL, 10);
    xcb_get_property_reply_t *list =
        !best && root ? property(c, root, clients, XCB_ATOM_WINDOW, 4096) : NULL;
    size_t best_score = SIZE_MAX;
    if (list && list->format == 32) {
        const xcb_window_t *windows = xcb_get_property_value(list);
        int total = xcb_get_property_value_length(list) / 4;
        for (int i = 0; i < total; ++i) {
            xcb_get_property_reply_t *pid = property(c, windows[i], pid_atom, XCB_ATOM_CARDINAL, 1);
            long value = pid && pid->format == 32 && xcb_get_property_value_length(pid) == 4
                             ? (long)*(const uint32_t *)xcb_get_property_value(pid)
                             : 0;
            free(pid);
            if (rank(pids, count, value) == SIZE_MAX)
                continue;
            xcb_get_property_reply_t *name =
                property(c, windows[i], name_atom, XCB_GET_PROPERTY_TYPE_ANY, 256);
            char caption[1025] = "";
            if (name) {
                int length = xcb_get_property_value_length(name);
                if (length > 1024)
                    length = 1024;
                memcpy(caption, xcb_get_property_value(name), (size_t)length);
                caption[length] = 0;
            }
            free(name);
            size_t candidate = score(pids, count, value, caption, r->title);
            if (candidate < best_score) {
                best = windows[i];
                best_score = candidate;
            }
        }
    }
    free(list);
    if (best && root && active != XCB_ATOM_NONE) {
        xcb_client_message_event_t event = {
            .response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = best, .type = active};
        event.data.data32[0] = 2; /* Source: pager, which managers honor. */
        event.data.data32[1] = XCB_CURRENT_TIME;
        xcb_send_event(c, 0, root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                       (const char *)&event);
        xcb_flush(c);
    }
    xcb_disconnect(c);
    return best != 0;
}
#endif

static gpointer focus(gpointer data)
{
    struct request *r = data;
    long start = select_tmux(r);
    bool tab = start != 0;
    if (!start) {
        bool konsole = select_konsole(r);
        tab = select_wezterm(r) || konsole;
    }
    if (start >= 0) {
        if (tab)
            g_usleep(150 * 1000); /* Lets the window title follow the new tab. */
        long pids[ANCESTOR_LIMIT];
        size_t count = ancestors(start ? start : (long)getpid(), pids, ANCESTOR_LIMIT);
        bool done = false;
        if (count && r->bus && listed(r->desktop, "KDE"))
            done = kwin(r, pids, count);
        if (count && !done && *r->sway)
            done = sway(r, pids, count);
        if (count && !done && *r->hyprland)
            done = hyprland(r, pids, count);
#ifdef HAVE_XCB
        if (count && !done && *r->display)
            x11(r, pids, count, start > 0);
#endif
    }
    g_clear_object(&r->bus);
    free(r);
    return NULL;
}
void terminal_focus_request(GDBusConnection *bus, const char *title)
{
    struct request *r = calloc(1, sizeof(*r));
    if (!r)
        return;
    r->bus = bus ? g_object_ref(bus) : NULL;
    snprintf(r->title, sizeof(r->title), "%s", title ? title : "");
    const char *tmux = getenv("TMUX");
    r->tmux = tmux && *tmux;
    copy_env(r->tmux_pane, sizeof(r->tmux_pane), "TMUX_PANE");
    copy_env(r->konsole_service, sizeof(r->konsole_service), "KONSOLE_DBUS_SERVICE");
    copy_env(r->konsole_window, sizeof(r->konsole_window), "KONSOLE_DBUS_WINDOW");
    copy_env(r->konsole_session, sizeof(r->konsole_session), "KONSOLE_DBUS_SESSION");
    copy_env(r->wezterm_pane, sizeof(r->wezterm_pane), "WEZTERM_PANE");
    copy_env(r->wezterm_dir, sizeof(r->wezterm_dir), "WEZTERM_EXECUTABLE_DIR");
    copy_env(r->desktop, sizeof(r->desktop), "XDG_CURRENT_DESKTOP");
    copy_env(r->sway, sizeof(r->sway), "SWAYSOCK");
    copy_env(r->hyprland, sizeof(r->hyprland), "HYPRLAND_INSTANCE_SIGNATURE");
    copy_env(r->runtime, sizeof(r->runtime), "XDG_RUNTIME_DIR");
    copy_env(r->display, sizeof(r->display), "DISPLAY");
    copy_env(r->window, sizeof(r->window), "WINDOWID");
    GThread *thread = g_thread_try_new("papo-focus", focus, r, NULL);
    if (!thread) {
        g_clear_object(&r->bus);
        free(r);
        return;
    }
    g_thread_unref(thread);
}
