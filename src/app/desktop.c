#define _POSIX_C_SOURCE 200809L
#include "app/desktop.h"
#include "net/clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_GIO
#include <gio/gio.h>
struct slot { char key[2120],title[1025],body[2049]; unsigned count,id; uint64_t due,serial; bool busy; };
struct desktop_notifier {
    GMainContext *context;
    GDBusConnection *bus;
    GCancellable *cancel;
    struct slot slots[64];
    unsigned refs,subscription;
    uint64_t epoch;
    bool dying,ready,markup,probing,attempted;
    char notice[128];
};
struct delivery { desktop_notifier *n; size_t slot; uint64_t serial,epoch; };
struct capability_request { desktop_notifier *n; uint64_t epoch; };
static void release(desktop_notifier *n)
{
    if (--n->refs) return;
    g_clear_object(&n->bus); g_clear_object(&n->cancel); g_main_context_unref(n->context); free(n);
}
static void unavailable(desktop_notifier *n)
{ n->ready=false; if (n->attempted) snprintf(n->notice,sizeof(n->notice),"Avisos desktop indisponíveis; central mantida."); }
static void close_notification(desktop_notifier *n,unsigned id)
{
    if (!n->bus || !id) return;
    g_dbus_connection_call(n->bus,"org.freedesktop.Notifications","/org/freedesktop/Notifications",
        "org.freedesktop.Notifications","CloseNotification",g_variant_new("(u)",id),NULL,
        G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,NULL,NULL,NULL);
}
static void capabilities(GObject *source,GAsyncResult *result,gpointer context)
{
    struct capability_request *request=context; desktop_notifier *n=request->n; GError *error=NULL;
    GVariant *reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);
    if (n->epoch==request->epoch) n->probing=false;
    if (!n->dying && n->epoch==request->epoch) {
        if (!reply) unavailable(n);
        else {
            gchar **caps=NULL; g_variant_get(reply,"(^as)",&caps);
            n->markup=false;
            for (size_t i=0;caps && caps[i];++i) if (!strcmp(caps[i],"body-markup")) n->markup=true;
            g_strfreev(caps); n->ready=true; n->notice[0]=0;
        }
    }
    if (reply) g_variant_unref(reply);
    g_clear_error(&error); free(request); release(n);
}
static void probe(desktop_notifier *n)
{
    if (!n->bus || n->probing || n->dying) return;
    struct capability_request *request=malloc(sizeof(*request));
    if (!request) return;
    *request=(struct capability_request){n,n->epoch}; n->probing=true; ++n->refs;
    g_main_context_push_thread_default(n->context);
    g_dbus_connection_call(n->bus,"org.freedesktop.Notifications","/org/freedesktop/Notifications",
        "org.freedesktop.Notifications","GetCapabilities",NULL,G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,n->cancel,capabilities,request);
    g_main_context_pop_thread_default(n->context);
}
static void owner_changed(GDBusConnection *bus,const gchar *sender,const gchar *path,const gchar *iface,
                          const gchar *signal,GVariant *parameters,gpointer context)
{
    (void)bus; (void)sender; (void)path; (void)iface; (void)signal;
    desktop_notifier *n=context; const char *name,*old,*owner;
    g_variant_get(parameters,"(&s&s&s)",&name,&old,&owner); (void)name; (void)old;
    if (n->dying) return;
    ++n->epoch; n->ready=n->probing=false;
    for (size_t i=0;i<64;++i) { n->slots[i].id=n->slots[i].count=0; ++n->slots[i].serial; }
    if (*owner) probe(n);
}
static void signal_released(gpointer context) { release(context); }
static void bus_ready(GObject *source,GAsyncResult *result,gpointer context)
{
    (void)source; desktop_notifier *n=context; GError *error=NULL;
    GDBusConnection *bus=g_bus_get_finish(result,&error);
    if (!n->dying && bus) {
        n->bus=bus; ++n->refs; g_main_context_push_thread_default(n->context);
        n->subscription=g_dbus_connection_signal_subscribe(bus,"org.freedesktop.DBus","org.freedesktop.DBus","NameOwnerChanged",
            "/org/freedesktop/DBus","org.freedesktop.Notifications",G_DBUS_SIGNAL_FLAGS_NONE,owner_changed,n,signal_released);
        g_main_context_pop_thread_default(n->context); probe(n);
    }
    else { if (bus) g_object_unref(bus); if (!n->dying) unavailable(n); }
    g_clear_error(&error); release(n);
}
desktop_notifier *desktop_create(void)
{
    desktop_notifier *n=calloc(1,sizeof(*n));
    if (!n) return NULL;
    n->refs=1; n->context=g_main_context_new(); n->cancel=g_cancellable_new();
    ++n->refs; g_main_context_push_thread_default(n->context);
    g_bus_get(G_BUS_TYPE_SESSION,n->cancel,bus_ready,n);
    g_main_context_pop_thread_default(n->context); return n;
}
static void utf8_copy(char *out,size_t capacity,const char *source)
{
    if (!source || !g_utf8_validate(source,-1,NULL)) source="Texto indisponível";
    size_t length=strlen(source);
    if (length>=capacity) { length=capacity-1; while (length && ((unsigned char)source[length]&0xc0)==0x80) --length; }
    memcpy(out,source,length); out[length]=0;
}
void desktop_queue(desktop_notifier *n,const char *key,const char *title,const char *body)
{
    if (!n || n->dying || !key || strlen(key)>=sizeof(n->slots[0].key)) return;
    n->attempted=true;
    uint64_t now; if (!net_now_ms(&now)) return;
    size_t at=64;
    for (size_t i=0;i<64;++i) if (!strcmp(n->slots[i].key,key)) { at=i; break; }
    if (at==64) for (size_t i=0;i<64;++i) if (!*n->slots[i].key && !n->slots[i].busy) { at=i; break; }
    if (at==64) return;
    struct slot *s=&n->slots[at];
    if (!s->count) s->due=now; /* Instances group and recheck eligibility for 2s. */
    snprintf(s->key,sizeof(s->key),"%s",key); utf8_copy(s->title,sizeof(s->title),title);
    utf8_copy(s->body,sizeof(s->body),body); if (s->count<1000000) ++s->count;
    if (!n->ready) probe(n);
}
static void sent(GObject *source,GAsyncResult *result,gpointer context)
{
    struct delivery *d=context; desktop_notifier *n=d->n;
    GError *error=NULL; GVariant *reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);
    struct slot *s=&n->slots[d->slot];
    if (reply) {
        guint id; g_variant_get(reply,"(u)",&id);
        if (!n->dying && s->serial==d->serial && n->epoch==d->epoch) s->id=id;
        else if (!n->dying && n->epoch==d->epoch) close_notification(n,id);
        g_variant_unref(reply);
    } else if (!n->dying && n->epoch==d->epoch) unavailable(n);
    s->busy=false;
    g_clear_error(&error); free(d); release(n);
}
void desktop_cancel(desktop_notifier *n,const char *key)
{
    if (!n) return;
    for (size_t i=0;i<64;++i) if (!key || !strcmp(n->slots[i].key,key)) {
        struct slot *s=&n->slots[i]; close_notification(n,s->id);
        s->id=s->count=0; s->key[0]=0; ++s->serial;
    }
}
bool desktop_poll(desktop_notifier *n)
{
    if (!n) return false;
    char old[128]; snprintf(old,sizeof(old),"%s",n->notice);
    for (int i=0;i<32 && g_main_context_pending(n->context);++i) g_main_context_iteration(n->context,FALSE);
    uint64_t now; if (!net_now_ms(&now)) return false;
    for (size_t i=0;i<64;++i) {
        struct slot *s=&n->slots[i];
        if (!s->count || s->busy || now<s->due) continue;
        unsigned count=s->count; s->count=0;
        if (!n->ready) continue; /* No replay of notifications after outage. */
        struct delivery *d=malloc(sizeof(*d)); if (!d) continue;
        char body[2200]; snprintf(body,sizeof(body),count>1 ? "%s\n%u novos avisos" : "%s",s->body,count);
        char *escaped=n->markup ? g_markup_escape_text(body,-1) : NULL;
        GVariantBuilder hints; g_variant_builder_init(&hints,G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&hints,"{sv}","urgency",g_variant_new_byte(1));
        *d=(struct delivery){n,i,s->serial,n->epoch}; ++n->refs; s->busy=true;
        g_main_context_push_thread_default(n->context);
        g_dbus_connection_call(n->bus,"org.freedesktop.Notifications","/org/freedesktop/Notifications",
            "org.freedesktop.Notifications","Notify",g_variant_new("(susss@as@a{sv}i)","Papo TUI",s->id,
                "mail-unread",s->title,escaped ? escaped : body,g_variant_new_strv(NULL,0),g_variant_builder_end(&hints),-1),
            G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,n->cancel,sent,d);
        g_main_context_pop_thread_default(n->context); g_free(escaped);
    }
    return strcmp(old,n->notice)!=0;
}
const char *desktop_notice(const desktop_notifier *n) { return n ? n->notice : ""; }
void desktop_destroy(desktop_notifier *n)
{
    if (!n) return;
    desktop_cancel(n,NULL); n->dying=true; g_cancellable_cancel(n->cancel);
    if (n->subscription) g_dbus_connection_signal_unsubscribe(n->bus,n->subscription);
    /* Cancellation callbacks release retained owners; no blocking bus calls. */
    uint64_t start=0,now=0; net_now_ms(&start);
    do {
        for (int i=0;i<64 && g_main_context_pending(n->context);++i) g_main_context_iteration(n->context,FALSE);
        if (n->refs==1 || !net_now_ms(&now) || now-start>=250) break;
        const struct timespec interval={.tv_nsec=1000000}; nanosleep(&interval,NULL);
    } while (true);
    release(n);
}
#else
struct desktop_notifier { bool attempted; };
desktop_notifier *desktop_create(void) { return calloc(1,sizeof(desktop_notifier)); }
void desktop_destroy(desktop_notifier *n) { free(n); }
void desktop_queue(desktop_notifier *n,const char *key,const char *title,const char *body)
{ if (n) n->attempted=true; (void)key; (void)title; (void)body; }
void desktop_cancel(desktop_notifier *n,const char *key) { (void)n; (void)key; }
bool desktop_poll(desktop_notifier *n) { (void)n; return false; }
const char *desktop_notice(const desktop_notifier *n) { return n && n->attempted ? "Desktop desabilitado neste build; central disponível." : ""; }
#endif
