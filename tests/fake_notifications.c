#define _POSIX_C_SOURCE 200809L
#include <gio/gio.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char xml[]=
"<node><interface name='org.freedesktop.Notifications'>"
"<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
"<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
"<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
"<arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
"<arg type='u' direction='out'/></method>"
"<method name='CloseNotification'><arg type='u' direction='in'/></method>"
"</interface></node>";
static unsigned sequence;
static gboolean delayed(gpointer data)
{ GDBusMethodInvocation *call=data; g_dbus_method_invocation_return_value(call,g_variant_new("(u)",++sequence)); g_object_unref(call); return G_SOURCE_REMOVE; }
static void called(GDBusConnection *bus,const gchar *sender,const gchar *path,const gchar *iface,const gchar *method,
                   GVariant *params,GDBusMethodInvocation *call,gpointer context)
{
    (void)bus; (void)sender; (void)path; (void)iface; (void)context;
    if (!strcmp(method,"GetCapabilities")) {
        const char *caps[]={"body","body-markup",NULL};
        g_dbus_method_invocation_return_value(call,g_variant_new("(^as)",caps));
    } else if (!strcmp(method,"Notify")) {
        GVariant *title=g_variant_get_child_value(params,3),*body=g_variant_get_child_value(params,4),*actions=g_variant_get_child_value(params,5);
        json_t *record=json_pack("{s:s,s:s,s:i}","title",g_variant_get_string(title,NULL),"body",g_variant_get_string(body,NULL),"actions",(int)g_variant_n_children(actions));
        json_dumpf(record,stdout,JSON_COMPACT); putchar('\n'); fflush(stdout); json_decref(record);
        g_variant_unref(title); g_variant_unref(body); g_variant_unref(actions);
        const char *delay=getenv("PAPO_NOTIFY_TEST_DELAY");
        if (delay) g_timeout_add((guint)atoi(delay),delayed,g_object_ref(call));
        else g_dbus_method_invocation_return_value(call,g_variant_new("(u)",++sequence));
    } else g_dbus_method_invocation_return_value(call,g_variant_new("()"));
}
int main(void)
{
    GError *error=NULL; GDBusConnection *bus=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,&error);
    if (!bus) { g_clear_error(&error); return 1; }
    GDBusNodeInfo *node=g_dbus_node_info_new_for_xml(xml,&error);
    const GDBusInterfaceVTable table={.method_call=called};
    guint registered=g_dbus_connection_register_object(bus,"/org/freedesktop/Notifications",node->interfaces[0],&table,NULL,NULL,&error);
    GVariant *reply=g_dbus_connection_call_sync(bus,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus",
        "RequestName",g_variant_new("(su)","org.freedesktop.Notifications",0U),G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NONE,2000,NULL,&error);
    if (!registered || !reply) { g_clear_error(&error); g_dbus_node_info_unref(node); g_object_unref(bus); return 2; }
    g_variant_unref(reply); puts("ready"); fflush(stdout);
    GMainLoop *loop=g_main_loop_new(NULL,FALSE); g_main_loop_run(loop);
    g_main_loop_unref(loop); g_dbus_connection_unregister_object(bus,registered); g_dbus_node_info_unref(node); g_object_unref(bus); return 0;
}
