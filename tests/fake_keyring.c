/* Synthetic credential service. Run ONLY in dbus-run-session with disposable
 * HOME/XDG directories. Secrets stay in RAM; control methods return counts. */
#include <gio/gio.h>
#include <jansson.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define COLLECTION "/org/freedesktop/secrets/collection/test"
#define SESSION "/org/freedesktop/secrets/session/test"
typedef struct { char *key, *secret, *path; GVariant *attributes; } item;
static item items[128]; static size_t count;
static char mode[32]; static guint stores, loads, deletes;
static bool folder;
static GDBusConnection *bus;
static GDBusNodeInfo *info;
static const char xml[] =
"<node>"
"<interface name='org.papo.SessionTest'>"
"<method name='Mode'><arg type='s' direction='in'/></method>"
"<method name='Counts'><arg type='u' direction='out'/><arg type='u' direction='out'/><arg type='u' direction='out'/></method>"
"<method name='Tamper'><arg type='s' direction='in'/></method>"
"</interface>"
"<interface name='org.kde.KWallet'>"
"<method name='networkWallet'><arg type='s' direction='out'/></method>"
"<method name='openAsync'><arg type='s' direction='in'/><arg type='x' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='in'/><arg type='i' direction='out'/></method>"
"<signal name='walletAsyncOpened'><arg type='i'/><arg type='i'/></signal>"
"<method name='hasFolder'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
"<method name='createFolder'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
"<method name='readPassword'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
"<method name='writePassword'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='i' direction='out'/></method>"
"<method name='removeEntry'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='i' direction='out'/></method>"
"<method name='close'><arg type='i' direction='in'/><arg type='b' direction='in'/><arg type='s' direction='in'/><arg type='i' direction='out'/></method>"
"</interface>"
"<interface name='org.freedesktop.Secret.Service'>"
"<method name='OpenSession'><arg type='s' direction='in'/><arg type='v' direction='in'/><arg type='v' direction='out'/><arg type='o' direction='out'/></method>"
"<method name='ReadAlias'><arg type='s' direction='in'/><arg type='o' direction='out'/></method>"
"<method name='SearchItems'><arg type='a{ss}' direction='in'/><arg type='ao' direction='out'/><arg type='ao' direction='out'/></method>"
"<method name='Unlock'><arg type='ao' direction='in'/><arg type='ao' direction='out'/><arg type='o' direction='out'/></method>"
"<method name='GetSecrets'><arg type='ao' direction='in'/><arg type='o' direction='in'/><arg type='a{o(oayays)}' direction='out'/></method>"
"<property name='Collections' type='ao' access='read'/></interface>"
"<interface name='org.freedesktop.Secret.Collection'>"
"<method name='CreateItem'><arg type='a{sv}' direction='in'/><arg type='(oayays)' direction='in'/><arg type='b' direction='in'/><arg type='o' direction='out'/><arg type='o' direction='out'/></method>"
"<property name='Items' type='ao' access='read'/><property name='Label' type='s' access='read'/><property name='Locked' type='b' access='read'/><property name='Created' type='t' access='read'/><property name='Modified' type='t' access='read'/></interface>"
"<interface name='org.freedesktop.Secret.Item'>"
"<method name='GetSecret'><arg type='o' direction='in'/><arg type='(oayays)' direction='out'/></method>"
"<method name='Delete'><arg type='o' direction='out'/></method>"
"<property name='Attributes' type='a{ss}' access='read'/><property name='Label' type='s' access='read'/><property name='Locked' type='b' access='read'/><property name='Created' type='t' access='read'/><property name='Modified' type='t' access='read'/></interface>"
"<interface name='org.freedesktop.Secret.Session'><method name='Close'/></interface>"
"</node>";
static void wipe(char *value) { if (value) { volatile char *p = value; for (size_t i = 0, n = strlen(value); i < n; ++i) p[i] = 0; g_free(value); } }
static item *find(const char *key, const char *path)
{
    for (size_t i = 0; i < count; ++i) if ((key && !strcmp(key, items[i].key)) || (path && !strcmp(path, items[i].path))) return items + i;
    return NULL;
}
static GVariant *secret(item *i)
{
    const char *value = i && i->secret ? i->secret : "";
    return g_variant_new("(o@ay@ays)", SESSION, g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, NULL, 0, 1),
                         g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, value, strlen(value), 1), "application/json");
}
static GVariant *property(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *interface,
                          const gchar *name, GError **error, gpointer data)
{
    (void)c; (void)sender; (void)interface; (void)error; (void)data;
    if (!strcmp(name, "Label")) return g_variant_new_string("Synthetic test collection");
    if (!strcmp(name, "Locked")) return g_variant_new_boolean(false);
    if (!strcmp(name, "Created") || !strcmp(name, "Modified")) return g_variant_new_uint64(1);
    if (!strcmp(name, "Attributes")) { item *i = find(NULL, path); return i ? g_variant_ref(i->attributes) : NULL; }
    GVariantBuilder b; g_variant_builder_init(&b, G_VARIANT_TYPE("ao"));
    if (!strcmp(name, "Collections")) g_variant_builder_add(&b, "o", COLLECTION);
    else for (size_t i = 0; i < count; ++i) if (items[i].secret) g_variant_builder_add(&b, "o", items[i].path);
    return g_variant_builder_end(&b);
}
static void method(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *, GDBusMethodInvocation *, gpointer);
static const GDBusInterfaceVTable vtable = {method, property, NULL, {NULL}};
static item *ensure(const char *key)
{
    item *i = find(key, NULL); if (i) return i;
    if (count == 128) return NULL;
    i = items + count++; i->key = g_strdup(key); i->path = g_strdup_printf(COLLECTION "/i%zu", count);
    GError *error = NULL;
    g_dbus_connection_register_object(bus, i->path, g_dbus_node_info_lookup_interface(info, "org.freedesktop.Secret.Item"), &vtable, NULL, NULL, &error);
    if (error) exit(2);
    return i;
}
typedef struct { GDBusMethodInvocation *call; GVariant *reply; } deferred;
static gboolean later(gpointer data)
{
    deferred *d = data; g_dbus_method_invocation_return_value(d->call, d->reply); g_object_unref(d->call); free(d); return G_SOURCE_REMOVE;
}
static void answer(GDBusMethodInvocation *call, GVariant *reply, bool write)
{
    if (write && !strcmp(mode, "slow")) {
        deferred *d = calloc(1, sizeof(*d)); d->call = g_object_ref(call); d->reply = reply; g_timeout_add(1200, later, d);
    } else g_dbus_method_invocation_return_value(call, reply);
}
static void method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *interface,
                    const gchar *name, GVariant *args, GDBusMethodInvocation *call, gpointer data)
{
    (void)c; (void)sender; (void)data;
    if (getenv("PAPO_TEST_DBUS_TRACE")) { fprintf(stderr, "%s.%s\n", interface, name); fflush(stderr); }
    if (!strcmp(interface, "org.papo.SessionTest")) {
        if (!strcmp(name, "Mode")) { const char *value; g_variant_get(args, "(&s)", &value); snprintf(mode, sizeof(mode), "%s", value); }
        else if (!strcmp(name, "Counts")) { answer(call, g_variant_new("(uuu)", loads, stores, deletes), false); return; }
        else if (!strcmp(name, "Tamper")) {
            const char *kind; g_variant_get(args, "(&s)", &kind);
            for (size_t n = 0; n < count; ++n) if (items[n].secret) {
                json_error_t error; json_t *obj = json_loads(items[n].secret, 0, &error);
                if (!strcmp(kind, "identity")) json_object_set_new(obj, "user_id", json_string("00000000-0000-4000-8000-000000000099"));
                else if (!strcmp(kind, "url")) json_object_set_new(obj, "url", json_string("https://foreign.invalid"));
                char *text = obj ? json_dumps(obj, JSON_COMPACT) : NULL;
                json_decref(obj); wipe(items[n].secret); items[n].secret = text;
            }
        }
        answer(call, NULL, false); return;
    }
    if (!strcmp(mode, "locked")) { g_dbus_method_invocation_return_dbus_error(call, "org.freedesktop.DBus.Error.AccessDenied", "Synthetic wallet denied"); return; }
    if (!strcmp(name, "networkWallet")) answer(call, g_variant_new("(s)", "test"), false);
    else if (!strcmp(name, "openAsync")) {
        if (strcmp(mode, "wait"))
            g_dbus_connection_emit_signal(bus, NULL, path, "org.kde.KWallet", "walletAsyncOpened", g_variant_new("(ii)", 1, 5), NULL);
        answer(call, g_variant_new("(i)", 1), false);
    } else if (!strcmp(name, "hasFolder")) answer(call, g_variant_new("(b)", folder), false);
    else if (!strcmp(name, "createFolder")) { bool fresh = !folder; folder = true; answer(call, g_variant_new("(b)", fresh), false); }
    else if (!strcmp(name, "close")) answer(call, g_variant_new("(i)", 0), false);
    else if (!strcmp(name, "readPassword") || !strcmp(name, "removeEntry") || !strcmp(name, "writePassword")) {
        gint handle; const char *f, *key, *app, *value = NULL;
        if (!strcmp(name, "writePassword")) g_variant_get(args, "(i&s&s&s&s)", &handle, &f, &key, &value, &app);
        else g_variant_get(args, "(i&s&s&s)", &handle, &f, &key, &app);
        item *i = find(key, NULL);
        if (!strcmp(name, "readPassword")) { ++loads; answer(call, g_variant_new("(s)", i && i->secret ? i->secret : ""), false); }
        else if (!strcmp(name, "removeEntry")) { ++deletes; if (i) { wipe(i->secret); i->secret = NULL; } answer(call, g_variant_new("(i)", 0), false); }
        else if (!strcmp(mode, "fail")) answer(call, g_variant_new("(i)", -1), false);
        else { i = ensure(key); wipe(i->secret); i->secret = g_strdup(value); ++stores; answer(call, g_variant_new("(i)", 0), true); }
    } else if (!strcmp(name, "OpenSession")) {
        const char *algorithm; GVariant *input; g_variant_get(args, "(&s@v)", &algorithm, &input); g_variant_unref(input);
        if (strcmp(algorithm, "plain")) g_dbus_method_invocation_return_dbus_error(call, "org.freedesktop.DBus.Error.NotSupported", "Synthetic plain session only");
        else answer(call, g_variant_new("(vo)", g_variant_new_string(""), SESSION), false);
    } else if (!strcmp(name, "ReadAlias")) answer(call, g_variant_new("(o)", COLLECTION), false);
    else if (!strcmp(name, "Unlock")) { GVariant *objects; g_variant_get(args, "(@ao)", &objects); answer(call, g_variant_new("(@aoo)", objects, "/"), false); }
    else if (!strcmp(name, "SearchItems")) {
        GVariant *attrs; g_variant_get(args, "(@a{ss})", &attrs); const char *scope = NULL; g_variant_lookup(attrs, "scope", "&s", &scope);
        item *i = scope ? find(scope, NULL) : NULL;
        GVariantBuilder b, empty; g_variant_builder_init(&b, G_VARIANT_TYPE("ao")); g_variant_builder_init(&empty, G_VARIANT_TYPE("ao"));
        if (i && i->secret) g_variant_builder_add(&b, "o", i->path);
        g_variant_unref(attrs); answer(call, g_variant_new("(@ao@ao)", g_variant_builder_end(&b), g_variant_builder_end(&empty)), false);
    } else if (!strcmp(name, "CreateItem")) {
        if (!strcmp(mode, "fail")) { g_dbus_method_invocation_return_dbus_error(call, "org.freedesktop.DBus.Error.Failed", "Synthetic write failed"); return; }
        GVariant *props, *s; gboolean replace; g_variant_get(args, "(@a{sv}@(oayays)b)", &props, &s, &replace);
        GVariant *attrs = g_variant_lookup_value(props, "org.freedesktop.Secret.Item.Attributes", G_VARIANT_TYPE("a{ss}"));
        const char *key; g_variant_lookup(attrs, "scope", "&s", &key); item *i = ensure(key);
        const char *session, *type; GVariant *parameters, *value;
        g_variant_get(s, "(&o@ay@ay&s)", &session, &parameters, &value, &type);
        gsize length; const char *bytes = g_variant_get_fixed_array(value, &length, 1);
        wipe(i->secret); i->secret = g_strndup(bytes, length); if (i->attributes) g_variant_unref(i->attributes); i->attributes = attrs;
        ++stores; answer(call, g_variant_new("(oo)", i->path, "/"), true);
        g_variant_unref(props); g_variant_unref(s); g_variant_unref(value); g_variant_unref(parameters);
    } else if (!strcmp(name, "GetSecret")) { ++loads; answer(call, g_variant_new("(@(oayays))", secret(find(NULL, path))), false); }
    else if (!strcmp(name, "GetSecrets")) {
        GVariant *objects; const char *session; g_variant_get(args, "(@ao&o)", &objects, &session);
        GVariantBuilder b; g_variant_builder_init(&b, G_VARIANT_TYPE("a{o(oayays)}"));
        GVariantIter it; const char *object; g_variant_iter_init(&it, objects);
        while (g_variant_iter_next(&it, "&o", &object)) { ++loads; g_variant_builder_add(&b, "{o@(oayays)}", object, secret(find(NULL, object))); }
        g_variant_unref(objects); answer(call, g_variant_new("(@a{o(oayays)})", g_variant_builder_end(&b)), false);
    } else if (!strcmp(name, "Delete")) {
        item *i = find(NULL, path); if (i) { wipe(i->secret); i->secret = NULL; } ++deletes; answer(call, g_variant_new("(o)", "/"), false);
    } else if (!strcmp(name, "Close")) answer(call, NULL, false);
    else g_dbus_method_invocation_return_dbus_error(call, "org.freedesktop.DBus.Error.UnknownMethod", "Synthetic method absent");
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    GError *error = NULL; info = g_dbus_node_info_new_for_xml(xml, &error); if (!info) return 2;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error); if (!bus) return 2;
    const char *name = !strcmp(argv[1], "secret") ? "org.freedesktop.secrets" : !strcmp(argv[1], "wallet5") ? "org.kde.kwalletd5" : "org.kde.kwalletd6";
    const char *path = !strcmp(argv[1], "wallet5") ? "/modules/kwalletd5" : "/modules/kwalletd6";
    for (size_t i = 0; info->interfaces[i]; ++i) {
        const char *object = !strcmp(info->interfaces[i]->name, "org.freedesktop.Secret.Collection") ? COLLECTION :
                             !strcmp(info->interfaces[i]->name, "org.freedesktop.Secret.Session") ? SESSION :
                             !strcmp(info->interfaces[i]->name, "org.freedesktop.Secret.Service") ? "/org/freedesktop/secrets" : path;
        if (!strcmp(info->interfaces[i]->name, "org.freedesktop.Secret.Item")) continue;
        if (!g_dbus_connection_register_object(bus, object, info->interfaces[i], &vtable, NULL, NULL, &error)) return 2;
    }
    if (!g_dbus_connection_register_object(bus, "/org/freedesktop/secrets/aliases/default",
         g_dbus_node_info_lookup_interface(info, "org.freedesktop.Secret.Collection"), &vtable, NULL, NULL, &error)) return 2;
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
                     g_variant_new("(su)", name, 0), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    if (!reply) return 2;
    guint ownership; g_variant_get(reply, "(u)", &ownership);
    if (ownership != 1) return 2;
    g_variant_unref(reply);
    puts("ready"); fflush(stdout); GMainLoop *loop = g_main_loop_new(NULL, false); g_main_loop_run(loop);
    return 0;
}
