#include "platform/file_transfer.h"

typedef struct {
    GDBusConnection *bus;
    char *destination;
    guint status;
    bool silent;
} service;
typedef struct {
    GCancellable *cancel;
    GPtrArray *paths;
    GError *error;
    gint done;
} request;
static const char *handle = "/org/freedesktop/portal/desktop/request/test/files";
static gboolean emit_response(gpointer data)
{
    service *s = data;
    GVariantBuilder results;
    g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
    const char *uris[] = {"file:///tmp/synthetic%20r%C3%A9sum%C3%A9.txt",
                          "file://remote-host/not-local", NULL};
    g_variant_builder_add(&results, "{sv}", "uris", g_variant_new_strv(uris, -1));
    g_dbus_connection_emit_signal(
        s->bus, s->destination, handle, "org.freedesktop.portal.Request", "Response",
        g_variant_new("(u@a{sv})", s->status, g_variant_builder_end(&results)), NULL);
    return G_SOURCE_REMOVE;
}
static void open_file(GDBusConnection *connection, const char *sender, const char *path,
                      const char *interface, const char *method, GVariant *parameters,
                      GDBusMethodInvocation *invocation, gpointer data)
{
    (void)connection;
    (void)path;
    (void)interface;
    service *s = data;
    g_assert_cmpstr(method, ==, "OpenFile");
    const char *parent, *title;
    GVariant *options;
    g_variant_get(parameters, "(&s&s@a{sv})", &parent, &title, &options);
    g_assert_cmpstr(parent, ==, "");
    g_assert_cmpstr(title, ==, "Anexar arquivos");
    gboolean multiple = false;
    g_assert_true(g_variant_lookup(options, "multiple", "b", &multiple));
    g_assert_true(multiple);
    g_variant_unref(options);
    g_free(s->destination);
    s->destination = g_strdup(sender);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", handle));
    if (!s->silent)
        g_timeout_add(25, emit_response, s);
}
static gpointer run_request(gpointer data)
{
    request *r = data;
    r->paths = platform_pick_files(r->cancel, &r->error);
    g_atomic_int_set(&r->done, 1);
    return NULL;
}
static void portal(void)
{
    GTestDBus *dbus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(dbus);
    g_setenv("WAYLAND_DISPLAY", "papo-synthetic-display", true);
    GError *error = NULL;
    service s = {0};
    s.bus =
        g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(dbus),
                                               G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                   G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                               NULL, NULL, &error);
    g_assert_no_error(error);
    GVariant *name = g_dbus_connection_call_sync(
        s.bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", "org.freedesktop.portal.Desktop", 0),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
    g_assert_no_error(error);
    g_variant_unref(name);
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.freedesktop.portal.FileChooser'>"
        "<method name='OpenFile'><arg type='s' direction='in'/><arg type='s' direction='in'/>"
        "<arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
        "</interface></node>",
        &error);
    g_assert_no_error(error);
    const GDBusInterfaceVTable vtable = {.method_call = open_file};
    guint object = g_dbus_connection_register_object(
        s.bus, "/org/freedesktop/portal/desktop", node->interfaces[0], &vtable, &s, NULL, &error);
    g_assert_no_error(error);
    for (int mode = 0; mode < 3; ++mode) {
        s.status = mode == 1 ? 1 : 0;
        s.silent = mode == 2;
        request r = {.cancel = g_cancellable_new()};
        GThread *thread = g_thread_new("portal-test", run_request, &r);
        gint64 start = g_get_monotonic_time();
        while (!g_atomic_int_get(&r.done) && g_get_monotonic_time() - start < 4000000) {
            while (g_main_context_iteration(NULL, false)) {
            }
            if (mode == 2 && g_get_monotonic_time() - start > 50000)
                g_cancellable_cancel(r.cancel);
            g_usleep(1000);
        }
        g_assert_cmpint(g_atomic_int_get(&r.done), ==, 1);
        g_thread_join(thread);
        if (!mode) {
            g_assert_no_error(r.error);
            g_assert_cmpuint(r.paths->len, ==, 1);
            g_assert_cmpstr(g_ptr_array_index(r.paths, 0), ==, "/tmp/synthetic résumé.txt");
        } else {
            g_assert_null(r.paths);
            g_assert_error(r.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        }
        g_clear_pointer(&r.paths, g_ptr_array_unref);
        g_clear_error(&r.error);
        g_object_unref(r.cancel);
    }
    g_dbus_connection_unregister_object(s.bus, object);
    g_dbus_node_info_unref(node);
    g_object_unref(s.bus);
    g_free(s.destination);
    g_test_dbus_down(dbus);
    g_object_unref(dbus);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/files/native-portal-success-cancel", portal);
    return g_test_run();
}
