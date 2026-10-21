#include "platform/file_transfer.h"
#include <string.h>

typedef struct {
    GVariant *response;
    const char *handle;
} portal_reply;
static void portal_response(GDBusConnection *bus, const char *sender, const char *path,
                            const char *interface, const char *signal, GVariant *parameters,
                            gpointer context)
{
    (void)bus;
    (void)sender;
    (void)interface;
    (void)signal;
    portal_reply *reply = context;
    if (reply->handle && !strcmp(path, reply->handle) && !reply->response)
        reply->response = g_variant_ref(parameters);
}
GPtrArray *platform_pick_files(GCancellable *cancel, GError **error)
{
    if ((!g_getenv("DISPLAY") || !*g_getenv("DISPLAY")) &&
        (!g_getenv("WAYLAND_DISPLAY") || !*g_getenv("WAYLAND_DISPLAY")) &&
        !g_getenv("FLATPAK_ID")) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Sem desktop local.");
        return NULL;
    }
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, cancel, error);
    GPtrArray *paths = NULL;
    portal_reply response = {0};
    guint subscription = bus ? g_dbus_connection_signal_subscribe(
                                   bus, "org.freedesktop.portal.Desktop",
                                   "org.freedesktop.portal.Request", "Response", NULL, NULL,
                                   G_DBUS_SIGNAL_FLAGS_NONE, portal_response, &response, NULL)
                             : 0;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(true));
    g_variant_builder_add(&options, "{sv}", "directory", g_variant_new_boolean(false));
    GVariant *reply =
        bus ? g_dbus_connection_call_sync(
                  bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                  "org.freedesktop.portal.FileChooser", "OpenFile",
                  g_variant_new("(ss@a{sv})", "", "Anexar arquivos",
                                g_variant_builder_end(&options)),
                  G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 2000, cancel, error)
            : NULL;
    if (!bus)
        g_variant_builder_clear(&options);
    if (reply) {
        g_variant_get(reply, "(&o)", &response.handle);
        while (!response.response && !g_cancellable_is_cancelled(cancel)) {
            for (int i = 0; i < 16 && g_main_context_iteration(context, false); ++i) {
            }
            g_usleep(10000);
        }
        if (g_cancellable_is_cancelled(cancel)) {
            g_dbus_connection_call(bus, "org.freedesktop.portal.Desktop", response.handle,
                                   "org.freedesktop.portal.Request", "Close", NULL, NULL,
                                   G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
            g_cancellable_set_error_if_cancelled(cancel, error);
        } else {
            guint status;
            GVariant *results;
            g_variant_get(response.response, "(u@a{sv})", &status, &results);
            if (status == 0) {
                paths = g_ptr_array_new_with_free_func(g_free);
                gchar **uris = NULL;
                g_variant_lookup(results, "uris", "^as", &uris);
                for (size_t i = 0; uris && uris[i]; ++i) {
                    char *host = NULL;
                    char *path = g_filename_from_uri(uris[i], &host, NULL);
                    if (path && (!host || !*host || !g_ascii_strcasecmp(host, "localhost")))
                        g_ptr_array_add(paths, path);
                    else
                        g_free(path);
                    g_free(host);
                }
                g_strfreev(uris);
            } else
                g_set_error_literal(error, G_IO_ERROR,
                                    status == 1 ? G_IO_ERROR_CANCELLED : G_IO_ERROR_NOT_SUPPORTED,
                                    "Seletor nativo fechado.");
            g_variant_unref(results);
        }
        g_variant_unref(reply);
    } else if (!g_cancellable_is_cancelled(cancel)) {
        g_clear_error(error);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Seletor nativo indisponível.");
    }
    if (subscription)
        g_dbus_connection_signal_unsubscribe(bus, subscription);
    g_clear_pointer(&response.response, g_variant_unref);
    g_clear_object(&bus);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return paths;
}

static GBytes *helper(const char *const *argv, gsize limit, GCancellable *cancel, GError **error)
{
    GSubprocess *process = g_subprocess_newv(
        argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
    if (!process)
        return NULL;
    GInputStream *stream = g_subprocess_get_stdout_pipe(process);
    GByteArray *bytes = g_byte_array_new();
    guint8 buffer[65536];
    bool ok = true;
    while (ok) {
        gssize n = g_input_stream_read(stream, buffer, sizeof(buffer), cancel, error);
        if (n < 0) {
            ok = false;
            break;
        }
        if (!n)
            break;
        if ((gsize)n > limit - bytes->len) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                "Conteúdo do clipboard excede o limite local.");
            ok = false;
        } else
            g_byte_array_append(bytes, buffer, (guint)n);
    }
    if (!ok)
        g_subprocess_force_exit(process);
    if (ok)
        ok = g_subprocess_wait_check(process, cancel, error);
    if (!ok)
        g_subprocess_force_exit(process);
    g_object_unref(process);
    if (!ok) {
        g_byte_array_unref(bytes);
        return NULL;
    }
    return g_byte_array_free_to_bytes(bytes);
}
static GBytes *read_type(bool wayland, const char *type, gsize limit, GCancellable *cancel,
                         GError **error)
{
    const char *wl[] = {"wl-paste", "--no-newline", "--type", type, NULL};
    const char *x11[] = {"xclip", "-selection", "clipboard", "-out", "-target", type, NULL};
    return helper(wayland ? wl : x11, limit, cancel, error);
}
static char *bytes_text(GBytes *bytes)
{
    gsize size;
    const char *data = g_bytes_get_data(bytes, &size);
    if (memchr(data, 0, size) || !g_utf8_validate(data, (gssize)size, NULL))
        return NULL;
    return g_strndup(data, size);
}
clipboard_content *platform_read_clipboard(GCancellable *cancel, GError **error)
{
    bool wayland = g_getenv("WAYLAND_DISPLAY") && *g_getenv("WAYLAND_DISPLAY");
    if (!wayland && (!g_getenv("DISPLAY") || !*g_getenv("DISPLAY"))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Clipboard indisponível; use Anexar/Ctrl+O ou a colagem do terminal.");
        return NULL;
    }
    const char *wl[] = {"wl-paste", "--list-types", NULL};
    const char *x11[] = {"xclip", "-selection", "clipboard", "-out", "-target", "TARGETS", NULL};
    GBytes *offered = helper(wayland ? wl : x11, 1024 * 1024, cancel, error);
    if (!offered)
        return NULL;
    char *types = bytes_text(offered);
    g_bytes_unref(offered);
    if (!types) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Formatos do clipboard inválidos.");
        return NULL;
    }
    const char *type = strstr(types, "x-special/gnome-copied-files")
                           ? "x-special/gnome-copied-files"
                       : strstr(types, "text/uri-list")            ? "text/uri-list"
                       : strstr(types, "image/png")                ? "image/png"
                       : strstr(types, "image/jpeg")               ? "image/jpeg"
                       : strstr(types, "text/plain;charset=utf-8") ? "text/plain;charset=utf-8"
                       : strstr(types, "UTF8_STRING")              ? "UTF8_STRING"
                       : strstr(types, "text/plain")               ? "text/plain"
                                                                   : NULL;
    g_free(types);
    if (!type) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Clipboard sem arquivos, imagem ou texto compatível.");
        return NULL;
    }
    bool image = g_str_has_prefix(type, "image/");
    GBytes *data =
        read_type(wayland, type, image ? ATTACHMENT_CLIPBOARD_LIMIT : 1024 * 1024, cancel, error);
    if (!data)
        return NULL;
    clipboard_content *content = g_new0(clipboard_content, 1);
    if (image) {
        content->image = g_bytes_ref(data);
        content->image_name =
            g_strdup(!strcmp(type, "image/png") ? "clipboard.png" : "clipboard.jpg");
    } else {
        char *text = bytes_text(data);
        if (text &&
            (!strcmp(type, "text/uri-list") || !strcmp(type, "x-special/gnome-copied-files"))) {
            content->paths = g_ptr_array_new_with_free_func(g_free);
            gchar **uris = g_uri_list_extract_uris(text);
            for (size_t i = 0; uris && uris[i]; ++i) {
                char *host = NULL, *path = g_filename_from_uri(uris[i], &host, NULL);
                if (path && (!host || !*host || !g_ascii_strcasecmp(host, "localhost")))
                    g_ptr_array_add(content->paths, path);
                else
                    g_free(path);
                g_free(host);
            }
            g_strfreev(uris);
            g_free(text);
        } else
            content->text = text;
    }
    g_bytes_unref(data);
    return content;
}
