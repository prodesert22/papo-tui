#include "ui/r8_modal.h"
#include "ui/mention_popup.h"
#include "ui/button.h"
#include "ui/theme.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>
struct r8_modal {
    history *h; auth_client *auth; profile_config *config;
    WINDOW *window;
    r8_modal_kind kind;
    bool open,detail,detail_event,prefs;
    size_t selected,first,detail_scroll;
    int focus,field_first;
    char detail_id[37],detail_message[37],destination[37],selected_id[37],notice[192];
    size_t users[10000],count;
    text_buffer filter;
};
static const char *s(const json_t *o,const char *k)
{ const char *v=json_string_value(json_object_get(o,k)); return v ? v : ""; }
const char *r8_presence_label(const char *state)
{
    return !strcmp(state,"online") ? "online" : !strcmp(state,"away") ? "ausente" :
           !strcmp(state,"busy") ? "ocupado" : !strcmp(state,"offline") ? "offline" : "?";
}
r8_modal *r8_modal_create(history *h,auth_client *auth,profile_config *config)
{ r8_modal *m=calloc(1,sizeof(*m)); if (m) { m->h=h; m->auth=auth; m->config=config; } return m; }
void r8_modal_close(r8_modal *m)
{
    if (!m) return;
    if (m->window) { delwin(m->window); m->window=NULL; }
    if (m->kind==R8_USERS) history_users_active(m->h,false);
    m->open=false;
}
void r8_modal_destroy(r8_modal *m) { if (m) { r8_modal_close(m); free(m); } }
void r8_modal_open(r8_modal *m,r8_modal_kind kind)
{
    r8_modal_close(m); m->kind=kind; m->open=true;
    m->selected=m->first=m->detail_scroll=0; m->focus=0; m->detail=m->prefs=false;
    m->detail_id[0]=m->detail_message[0]=m->selected_id[0]=m->notice[0]=0; text_decode(&m->filter,"");
    if (kind==R8_USERS) history_users_refresh(m->h);
    else if (kind==R8_NOTIFICATIONS) inbox_refresh(history_inbox(m->h));
}
bool r8_modal_is_open(const r8_modal *m) { return m && m->open; }
static json_t *notification(r8_modal *m,size_t index,bool *event)
{
    inbox *b=history_inbox(m->h); size_t count=inbox_notification_count(b);
    *event=index>=count;
    return *event ? inbox_notification_event(b,index-count) : inbox_notification(b,index);
}
static void update(r8_modal *m)
{
    inbox *b=history_inbox(m->h);
    if (m->kind!=R8_USERS) { m->count=inbox_notification_count(b)+inbox_event_count(b); }
    else {
        char query[TEXT_UTF8_BYTES]; text_encode(&m->filter,query); m->count=0;
        for (size_t i=0;i<history_user_count(m->h) && m->count<10000;++i) {
            json_t *u=history_user(m->h,i);
            if (!strcmp(s(u,"id"),auth_user_id(m->auth)) || json_is_true(json_object_get(u,"banned"))) continue;
            if (!*query || mention_name_matches(s(u,"username"),query) || mention_name_matches(s(u,"nickname"),query))
                m->users[m->count++]=i;
        }
    }
    if (*m->selected_id) for (size_t i=0;i<m->count;++i) {
        bool event; json_t *v=m->kind==R8_USERS ? history_user(m->h,m->users[i]) : notification(m,i,&event);
        if (!strcmp(s(v,"id"),m->selected_id)) { m->selected=i; break; }
    }
    if (m->selected>=m->count) m->selected=m->count ? m->count-1 : 0;
}
static json_t *detail(r8_modal *m,bool *event)
{
    inbox *b=history_inbox(m->h);
    for (size_t i=0;i<inbox_notification_count(b);++i) {
        json_t *n=inbox_notification(b,i);
        if (!strcmp(s(n,"id"),m->detail_id) ||
            (m->detail_event && *m->detail_message && !strcmp(s(n,"message_id"),m->detail_message))) { *event=false; return n; }
    }
    for (size_t i=0;i<inbox_event_count(b);++i) {
        json_t *n=inbox_notification_event(b,i);
        if (!strcmp(s(n,"id"),m->detail_id)) { *event=true; return n; }
    }
    *event=m->detail_event; return NULL;
}
static void activate(r8_modal *m)
{
    inbox *b=history_inbox(m->h);
    if (m->prefs) {
        if (!m->config) return;
        bool desktop=m->config->desktop_notifications,preview=m->config->notification_preview;
        if (m->focus==0) desktop=!desktop; else preview=!preview;
        if (!profiles_set_notifications(m->config,desktop,preview)) snprintf(m->notice,sizeof(m->notice),"%s",m->config->message);
        else m->notice[0]=0;
    } else if (m->detail) {
        bool event; json_t *n=detail(m,&event);
        const char *id=event ? inbox_message_channel(b,s(n,"message_id")) : s(n,"channel_id");
        if (history_uuid(id)) { snprintf(m->destination,sizeof(m->destination),"%s",id); r8_modal_close(m); }
        else snprintf(m->notice,sizeof(m->notice),"Destino ainda indisponível. Ctrl+R: atualizar.");
    } else if (m->kind==R8_USERS) {
        json_t *u=m->count ? history_user(m->h,m->users[m->selected]) : NULL;
        if (u && inbox_open_dm(b,s(u,"id"))) r8_modal_close(m);
    } else if (m->focus==1) { m->prefs=true; m->focus=0; }
    else if (m->focus==2) { inbox_older(b); }
    else if (m->kind==R8_NOTIFICATIONS && m->count) {
        bool event; json_t *n=notification(m,m->selected,&event);
        snprintf(m->detail_id,sizeof(m->detail_id),"%s",s(n,"id"));
        snprintf(m->detail_message,sizeof(m->detail_message),"%s",s(n,"message_id"));
        m->detail_event=event; m->detail=true; m->detail_scroll=0;
        /* Explicit user opening is evidence of focus even on terminals without
         * reporting. Automatic rendering of history requires confirmed focus. */
        inbox_view(b,m->detail_id,event);
    }
}
bool r8_modal_key(r8_modal *m,int type,wint_t key)
{
    if (!m || !m->open) return false;
    update(m); m->selected_id[0]=0;
    if (type==KEY_CODE_YES && key==KEY_RESIZE) { if (m->window) { delwin(m->window); m->window=NULL; } return true; }
    if (type==OK && key==27) {
        if (m->prefs) { m->prefs=false; m->focus=1; }
        else if (m->detail) { m->detail=false; m->focus=0; }
        else r8_modal_close(m);
        return true;
    }
    if (type==OK && key==18) {
        inbox_retry(history_inbox(m->h));
        if (m->kind==R8_USERS) history_users_refresh(m->h);
        m->notice[0]=0; return true;
    }
    if (type==KEY_CODE_YES && key==KEY_MOUSE) {
        MEVENT e;
        if (getmouse(&e)!=OK || !m->window) return true;
        int y=e.y,x=e.x;
        if (!wmouse_trafo(m->window,&y,&x,false)) { if (e.bstate&(BUTTON1_PRESSED|BUTTON1_CLICKED)) r8_modal_close(m); return true; }
        if (e.bstate&(BUTTON4_PRESSED|BUTTON5_PRESSED)) {
            return r8_modal_key(m,KEY_CODE_YES,e.bstate&BUTTON4_PRESSED ? KEY_UP : KEY_DOWN);
        }
        if (!(e.bstate&(BUTTON1_PRESSED|BUTTON1_CLICKED))) return true;
        if (m->prefs && (y==3 || y==5)) { m->focus=y==3 ? 0 : 1; activate(m); }
        else if (m->detail && y==getmaxy(m->window)-4) activate(m);
        else if (!m->detail && !m->prefs && y==getmaxy(m->window)-4 && m->kind==R8_NOTIFICATIONS) {
            m->focus=x<20 ? 1 : 2; activate(m);
        } else if (!m->detail && !m->prefs && y>=4 && y<getmaxy(m->window)-5 && m->first+(size_t)(y-4)<m->count) {
            m->selected=m->first+(size_t)(y-4); m->focus=0; activate(m);
        }
        return true;
    }
    if ((type==OK && key=='\t') || (type==KEY_CODE_YES && key==KEY_BTAB)) {
        int n=m->prefs ? 2 : m->kind==R8_NOTIFICATIONS && !m->detail ? 3 : 1;
        m->focus=(m->focus+(type==OK ? 1 : n-1))%n; return true;
    }
    bool enter=(type==OK && (key=='\n' || key=='\r')) || (type==KEY_CODE_YES && key==KEY_ENTER);
    if (enter || (type==OK && key==' ' && m->prefs)) { activate(m); return true; }
    if (m->kind==R8_HELP) return true;
    if (m->detail) {
        if (type==KEY_CODE_YES && (key==KEY_DOWN || key==KEY_NPAGE)) m->detail_scroll+=key==KEY_DOWN ? 1 : 5;
        if (type==KEY_CODE_YES && (key==KEY_UP || key==KEY_PPAGE)) m->detail_scroll=m->detail_scroll>5 ? m->detail_scroll-5 : 0;
        return true;
    }
    if (m->prefs) { if (type==KEY_CODE_YES && (key==KEY_UP || key==KEY_DOWN)) m->focus=key==KEY_UP ? 0 : 1; return true; }
    if (type==KEY_CODE_YES && key==KEY_UP && m->selected) --m->selected;
    else if (type==KEY_CODE_YES && key==KEY_DOWN && m->selected+1<m->count) ++m->selected;
    else if (type==KEY_CODE_YES && key==KEY_HOME) m->selected=0;
    else if (type==KEY_CODE_YES && key==KEY_END && m->count) m->selected=m->count-1;
    else if (type==KEY_CODE_YES && key==KEY_PPAGE) m->selected=m->selected>5 ? m->selected-5 : 0;
    else if (type==KEY_CODE_YES && key==KEY_NPAGE && m->count) { m->selected+=5; if (m->selected>=m->count) m->selected=m->count-1; }
    else if (m->kind==R8_USERS && ((type==OK && iswprint(key)) || type==KEY_CODE_YES || (type==OK && (key==3 || key==21 || key==127)))) {
        if (text_edit(&m->filter,type,key,60,false)) { m->selected=m->first=0; }
    }
    return true;
}
void r8_modal_draw(r8_modal *m)
{
    if (!m || !m->open || COLS<44 || LINES<14) return;
    int width=COLS-4; if (width>90) width=90;
    int height=LINES-2; if (height>25) height=25;
    int top=(LINES-height)/2,left=(COLS-width)/2;
    if (m->window && (getmaxx(m->window)!=width || getmaxy(m->window)!=height || getbegx(m->window)!=left || getbegy(m->window)!=top)) {
        delwin(m->window); m->window=NULL;
    }
    if (!m->window) m->window=newwin(height,width,top,left);
    if (!m->window) return;
    WINDOW *w=m->window; wbkgd(w,COLOR_PAIR(PANEL)); werase(w); box(w,0,0); update(m);
    const char *title=m->kind==R8_USERS ? "Nova DM" : m->kind==R8_HELP ? "Ajuda" : m->prefs ? "Preferências" : m->detail ? "Detalhes da notificação" : "Notificações";
    wattrset(w,COLOR_PAIR(PANEL_ACCENT)|A_BOLD); text_label(w,1,2,width-4,title); wattrset(w,COLOR_PAIR(PANEL));
    if (m->kind==R8_HELP) {
        const char *lines[]={"Tab/Shift+Tab: percorrer áreas","Canais/DMs: ←→ escolher, Enter ativar","↑↓: navegar · Enter: abrir texto/DM","Voz: F4 abre o histórico","F9: notificações · F10: nova DM","Ctrl+D na lista DMs: fechar conversa","Ctrl+R: atualizar ou reconectar","F6: conta · Ctrl+L: sair da conta","Esc: voltar · Ctrl+Q: sair"};
        for (size_t i=0;i<sizeof(lines)/sizeof(lines[0]) && (int)i+3<height-2;++i) text_label(w,(int)i+3,2,width-4,lines[i]);
    } else if (m->prefs) {
        button_draw(w,3,2,width-4,m->config && m->config->desktop_notifications ? "[x] Avisos no desktop" : "[ ] Avisos no desktop",m->focus==0 ? BUTTON_FOCUSED : BUTTON_NORMAL,BUTTON_PANEL);
        button_draw(w,5,2,width-4,m->config && m->config->notification_preview ? "[x] Mostrar prévia" : "[ ] Mostrar prévia",m->focus==1 ? BUTTON_FOCUSED : BUTTON_NORMAL,BUTTON_PANEL);
    } else if (m->detail) {
        bool event; json_t *n=detail(m,&event);
        const char *author=history_author(m->h,s(n,event ? "user_id" : "author_id"));
        text_label(w,3,2,width-4,author);
        text_buffer content; text_layout layout;
        text_decode(&content,n ? s(n,"message_content") : "Notificação indisponível. Atualize a lista.");
        text_measure(&content,width-4,&layout);
        size_t max=layout.rows>height-8 ? (size_t)(layout.rows-(height-8)) : 0;
        if (m->detail_scroll>max) m->detail_scroll=max;
        text_draw(w,&content,&layout,(int)m->detail_scroll,height-8,4,2);
        button_draw(w,height-4,2,width-4,"[Abrir conversa]",BUTTON_FOCUSED,BUTTON_PANEL);
    } else {
        if (m->kind==R8_USERS) text_draw_field(w,&m->filter,&m->field_first,2,2,width-4,false);
        else {
            char total[100]; snprintf(total,sizeof(total),"%zu não lidas conhecidas%s",inbox_unread(history_inbox(m->h)),inbox_has_more(history_inbox(m->h)) ? " · há mais páginas" : "");
            text_label(w,2,2,width-4,total);
        }
        int rows=height-9; if (rows<1) rows=1;
        if (m->selected<m->first) m->first=m->selected;
        if (m->selected>=m->first+(size_t)rows) m->first=m->selected-(size_t)rows+1;
        for (int row=0;row<rows && m->first+(size_t)row<m->count;++row) {
            size_t i=m->first+(size_t)row; char label[3200];
            json_t *v;
            if (m->kind==R8_USERS) {
                v=history_user(m->h,m->users[i]);
                snprintf(label,sizeof(label),"%s (@%s) · %s",*s(v,"nickname") ? s(v,"nickname") : s(v,"username"),s(v,"username"),r8_presence_label(inbox_presence(history_inbox(m->h),s(v,"id"))));
            } else {
                bool event; v=notification(m,i,&event);
                snprintf(label,sizeof(label),"%s%s · %s",event ? "Aviso · " : json_is_true(json_object_get(v,"read")) ? "" : "* ",history_author(m->h,s(v,event ? "user_id" : "author_id")),s(v,"message_content"));
            }
            wattrset(w,COLOR_PAIR(i==m->selected && m->focus==0 ? FOCUSED : PANEL)); text_label(w,row+4,2,width-4,label);
            if (i==m->selected) snprintf(m->selected_id,sizeof(m->selected_id),"%s",s(v,"id"));
        }
        wattrset(w,COLOR_PAIR(PANEL));
        if (!m->count) text_label(w,4,2,width-4,m->kind==R8_USERS ? "Nenhum usuário disponível neste filtro." : "Nenhuma notificação disponível.");
        if (m->kind==R8_NOTIFICATIONS) {
            button_draw(w,height-4,2,17,"[Preferências]",m->focus==1 ? BUTTON_FOCUSED : BUTTON_NORMAL,BUTTON_PANEL);
            button_draw(w,height-4,21,width-23,"[Mais antigas]",m->focus==2 ? BUTTON_FOCUSED : BUTTON_NORMAL,BUTTON_PANEL);
        }
    }
    wattrset(w,COLOR_PAIR(PANEL_MUTED));
    const char *notice=*m->notice ? m->notice : m->kind==R8_USERS ? history_users_notice(m->h) : inbox_notice(history_inbox(m->h));
    text_label(w,height-3,2,width-4,notice);
    text_label(w,height-2,2,width-4,"↑↓ escolher · Enter abrir · Ctrl+R tentar · Esc voltar");
    wnoutrefresh(w);
}
const char *r8_modal_destination(const r8_modal *m) { return m->destination; }
void r8_modal_destination_clear(r8_modal *m) { m->destination[0]=0; }
