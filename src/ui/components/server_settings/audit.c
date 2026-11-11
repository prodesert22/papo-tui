#include "model/text/translation.h"
#include "ui/components/server_settings/private.h"
#include "ui/media/message_time.h"
#include <time.h>

/* Auditoria: the server's audit log, newest first (manage_server). The first
 * row of the list opens the filters: entity, action, who did it and dates. */
enum {
    ENTITY,
    ACTION,
    SEARCH,
    ACTOR,
    ANYONE,
    FROM,
    UNTIL,
    APPLY,
    RESET,
    RETRY,
    ONLY_ACTION,
    ONLY_ACTOR
};
#define ROW_FILTERS "filters"
#define MATCHES 6
static const char *const entities[] = {"",     "user",      "server", "channel", "message",
                                       "attachment", "role", "user_role", "emoji",   "webhook"};
static const char *const entity_labels[] = {
    N_("Todas"),  N_("Usuários"), N_("Servidor"),          N_("Canais"), N_("Mensagens"),
    N_("Anexos"), N_("Cargos"),   N_("Cargos de membros"), N_("Emojis"), N_("Webhooks")};
#define ENTITIES ((int)G_N_ELEMENTS(entities))

static const json_t *selected_log(server_settings *s)
{
    size_t i;
    json_t *log;
    json_array_foreach(server_admin_items(s->model, ADMIN_LIST_AUDIT), i, log)
    {
        if (*s->selected && !strcmp(admin_str(log, "id"), s->selected))
            return log;
    }
    return NULL;
}
static int entity_index(const char *entity)
{
    for (int i = 1; i < ENTITIES; ++i)
        if (!strcmp(entity, entities[i]))
            return i;
    return 0;
}
static json_t *user_by_id(server_settings *s, const char *id)
{
    for (size_t i = 0; *id && i < history_user_count(s->model); ++i) {
        json_t *user = history_user(s->model, i);
        if (!strcmp(admin_str(user, "id"), id))
            return user;
    }
    return NULL;
}
/* Events name their actor only by username; the directory gives the ID. */
static json_t *user_by_name(server_settings *s, const char *username)
{
    for (size_t i = 0; *username && i < history_user_count(s->model); ++i) {
        json_t *user = history_user(s->model, i);
        if (!strcmp(admin_str(user, "username"), username))
            return user;
    }
    return NULL;
}
static int active_filters(const admin_audit_query *q)
{
    return !!*q->entity + !!*q->action + !!*q->actor + !!q->from + !!*q->until;
}

/* Dates are typed as DD/MM/AAAA in this computer's time zone. */
static bool parse_date(const char *text, bool end, GDateTime **out)
{
    int day, month, year;
    char tail;
    *out = NULL;
    if (!*text)
        return true;
    if (sscanf(text, "%d/%d/%d%c", &day, &month, &year, &tail) != 3 || year < 1970 || year > 9999 ||
        !g_date_valid_dmy((GDateDay)day, (GDateMonth)month, (GDateYear)year))
        return false;
    *out = end ? g_date_time_new_local(year, month, day, 23, 59, 59)
               : g_date_time_new_local(year, month, day, 0, 0, 0);
    return *out != NULL;
}
static void show_date(text_buffer *buffer, GDateTime *time)
{
    char *text = time ? g_date_time_format(time, "%d/%m/%Y") : NULL;
    admin_set_text(buffer, text ? text : "");
    g_free(text);
}

static void enter(server_settings *s)
{
    server_admin_members_active(s->model, true);
    admin_audit_query none;
    memset(&none, 0, sizeof(none));
    server_admin_audit_query(s->model, &none);
    admin_select(s, ROW_FILTERS);
}
static void rows(server_settings *s, GArray *out)
{
    int active = active_filters(server_admin_audit_current(s->model));
    char label[64];
    if (active)
        snprintf(label, sizeof(label),
                 P_("▾ Filtros (%d ativo)", "▾ Filtros (%d ativos)", (unsigned long)active),
                 active);
    else
        snprintf(label, sizeof(label), _("▾ Filtros"));
    admin_row_add(out, ROW_FILTERS, label, "", 0, true);
    size_t i;
    json_t *log;
    time_t now = time(NULL);
    json_array_foreach(server_admin_items(s->model, ADMIN_LIST_AUDIT), i, log)
    {
        char when[64], line[256];
        if (!message_time_format(admin_str(log, "created_at"), now, when, sizeof(when)))
            snprintf(when, sizeof(when), "—");
        snprintf(line, sizeof(line), "%s %s", when,
                 *admin_str(log, "actor_username") ? admin_str(log, "actor_username") : "—");
        admin_row_add(out, admin_str(log, "id"), line, admin_str(log, "action"), 0, false);
    }
    if (server_admin_more(s->model, ADMIN_LIST_AUDIT))
        admin_row_add(out, ROW_MORE,
                      server_admin_loading(s->model, ADMIN_LIST_AUDIT) ? _("Carregando…")
                                                                       : _("Carregar mais…"),
                      "", 0, true);
}
static void choose(server_settings *s, const admin_row *row)
{
    if (!strcmp(row->id, ROW_MORE))
        server_admin_load(s->model, ADMIN_LIST_AUDIT, true);
    else
        s->focus = 2;
}
/* The filter drafts start from the filters in use. */
static void fill(server_settings *s)
{
    const admin_audit_query *q = server_admin_audit_current(s->model);
    s->choice = entity_index(q->entity);
    snprintf(s->draft_action, sizeof(s->draft_action), "%s", q->action);
    snprintf(s->draft_actor, sizeof(s->draft_actor), "%s", q->actor);
    text_clear(&s->name);
    GDateTime *from = q->from ? g_date_time_new_from_unix_local((gint64)(q->from / 1000)) : NULL;
    GDateTime *until_utc = *q->until ? g_date_time_new_from_iso8601(q->until, NULL) : NULL;
    GDateTime *until = until_utc ? g_date_time_to_local(until_utc) : NULL;
    show_date(&s->aux, from);
    show_date(&s->extra, until);
    if (from)
        g_date_time_unref(from);
    if (until)
        g_date_time_unref(until);
    if (until_utc)
        g_date_time_unref(until_utc);
    s->name_first = s->aux_first = s->extra_first = 0;
}
static void filter_fields(server_settings *s, GArray *out)
{
    admin_field_add(out, FIELD_HEADING, 0, _("Filtros"));
    admin_field *f = admin_field_add(out, FIELD_CHOICE, ENTITY, _("Entidade"));
    snprintf(f->value, sizeof(f->value), "%s", _(entity_labels[s->choice]));
    f = admin_field_add(out, FIELD_CHOICE, ACTION, _("Ação"));
    snprintf(f->value, sizeof(f->value), "%s", *s->draft_action ? s->draft_action : _("Todas"));
    json_t *actor = user_by_id(s, s->draft_actor);
    if (*s->draft_actor) {
        f = admin_field_add(out, FIELD_INFO, 0, _("Feito por"));
        snprintf(f->value, sizeof(f->value), "%s (@%s)",
                 actor ? admin_user_display(actor) : s->draft_actor,
                 actor ? admin_str(actor, "username") : "?");
        admin_field_add(out, FIELD_BUTTON, ANYONE, _("Qualquer pessoa"));
    } else {
        f = admin_field_add(out, FIELD_TEXT, SEARCH, _("Feito por"));
        f->text = &s->name;
        f->first = &s->name_first;
        static char query[TEXT_UTF8_BYTES];
        bool typed = admin_text(&s->name, query) && *g_strstrip(query);
        if (!typed)
            admin_note(out, _("Digite parte do nome ou do @usuário de quem fez a ação."));
        else {
            char *folded = g_utf8_casefold(query, -1);
            int shown = 0;
            for (size_t i = 0; folded && i < history_user_count(s->model) && shown < MATCHES; ++i) {
                json_t *user = history_user(s->model, i);
                if (!admin_user_matches(user, folded))
                    continue;
                char label[160];
                snprintf(label, sizeof(label), "%s (@%s)", admin_user_display(user),
                         admin_str(user, "username"));
                f = admin_field_add(out, FIELD_BUTTON, ACTOR, label);
                snprintf(f->key, sizeof(f->key), "%s", admin_str(user, "id"));
                ++shown;
            }
            g_free(folded);
            if (!shown)
                admin_note(out, history_users_complete(s->model) ? _("Ninguém com esse nome.")
                                                                 : _("Carregando membros…"));
        }
    }
    f = admin_field_add(out, FIELD_TEXT, FROM, "De");
    f->text = &s->aux;
    f->first = &s->aux_first;
    f = admin_field_add(out, FIELD_TEXT, UNTIL, _("Até"));
    f->text = &s->extra;
    f->first = &s->extra_first;
    admin_note(out, _("Datas em DD/MM/AAAA, no fuso deste computador; incluem o dia todo."));
    admin_field_add(out, FIELD_BUTTON, APPLY, _("Aplicar filtros"));
    if (active_filters(server_admin_audit_current(s->model)))
        admin_field_add(out, FIELD_BUTTON, RESET, _("Limpar filtros"));
}
static void log_fields(server_settings *s, GArray *out, const json_t *log)
{
    admin_field_add(out, FIELD_HEADING, 0, admin_str(log, "action"));
    admin_field *f = admin_field_add(out, FIELD_INFO, 0, _("Data"));
    if (!message_time_format(admin_str(log, "created_at"), time(NULL), f->value, sizeof(f->value)))
        snprintf(f->value, sizeof(f->value), "—");
    const char *actor = admin_str(log, "actor_username");
    f = admin_field_add(out, FIELD_INFO, 0, _("Feito por"));
    snprintf(f->value, sizeof(f->value), "%s", *actor ? actor : "—");
    f = admin_field_add(out, FIELD_INFO, 0, _("Entidade"));
    const char *entity = admin_str(log, "entity_type");
    int index = entity_index(entity);
    snprintf(f->value, sizeof(f->value), "%s",
             index     ? _(entity_labels[index])
             : *entity ? entity
                       : "—");
    const char *target = admin_str(log, "target_user_id");
    if (*target) {
        f = admin_field_add(out, FIELD_INFO, 0, _("Membro afetado"));
        snprintf(f->value, sizeof(f->value), "%s", admin_user_name(s, target, target));
    }
    const json_t *metadata = json_object_get(log, "metadata");
    if (json_is_object(metadata) && json_object_size(metadata)) {
        admin_field_add(out, FIELD_HEADING, 0, _("Detalhes"));
        char *text = json_dumps(metadata, JSON_COMPACT | JSON_SORT_KEYS);
        admin_note(out, text ? text : "");
        free(text);
    }
    admin_field_add(out, FIELD_HEADING, 0, _("Filtrar a partir deste evento"));
    char label[192];
    snprintf(label, sizeof(label), _("Só a ação %s"), admin_str(log, "action"));
    f = admin_field_add(out, FIELD_BUTTON, ONLY_ACTION, label);
    snprintf(f->key, sizeof(f->key), "%.39s", admin_str(log, "id"));
    json_t *user = user_by_name(s, actor);
    if (user) {
        snprintf(label, sizeof(label), _("Só as ações de %s"), admin_user_display(user));
        f = admin_field_add(out, FIELD_BUTTON, ONLY_ACTOR, label);
        snprintf(f->key, sizeof(f->key), "%s", admin_str(user, "id"));
    }
}
static void fields(server_settings *s, GArray *out)
{
    if (!strcmp(s->selected, ROW_FILTERS) || !*s->selected) {
        filter_fields(s, out);
        if (server_admin_failed(s->model, ADMIN_LIST_AUDIT)) {
            admin_note(out, _("Não foi possível carregar o registro."));
            admin_field_add(out, FIELD_BUTTON, RETRY, _("Tentar de novo"));
        } else if (!server_admin_items(s->model, ADMIN_LIST_AUDIT))
            admin_note(out, _("Carregando registro…"));
        else if (!json_array_size(server_admin_items(s->model, ADMIN_LIST_AUDIT)))
            admin_note(out, _("Nenhum evento registrado com estes filtros."));
        return;
    }
    const json_t *log = selected_log(s);
    if (log)
        log_fields(s, out, log);
}
/* The filters in the form become the server query. */
static void apply(server_settings *s)
{
    static char from_text[TEXT_UTF8_BYTES], until_text[TEXT_UTF8_BYTES];
    if (!admin_text(&s->aux, from_text) || !admin_text(&s->extra, until_text)) {
        admin_invalid(s, FROM, _("Data inválida; use DD/MM/AAAA."));
        return;
    }
    GDateTime *from, *until;
    bool ok_from = parse_date(g_strstrip(from_text), false, &from),
         ok_until = parse_date(g_strstrip(until_text), true, &until);
    admin_audit_query q;
    memset(&q, 0, sizeof(q));
    snprintf(q.entity, sizeof(q.entity), "%s", entities[s->choice]);
    snprintf(q.action, sizeof(q.action), "%s", s->draft_action);
    snprintf(q.actor, sizeof(q.actor), "%s", s->draft_actor);
    if (from)
        q.from = (uint64_t)g_date_time_to_unix(from) * 1000U;
    if (until) {
        GDateTime *utc = g_date_time_to_utc(until);
        char *text = utc ? g_date_time_format(utc, "%Y-%m-%dT%H:%M:%SZ") : NULL;
        snprintf(q.until, sizeof(q.until), "%s", text ? text : "");
        g_free(text);
        if (utc)
            g_date_time_unref(utc);
    }
    bool ordered = !from || !until || g_date_time_compare(from, until) <= 0;
    if (from)
        g_date_time_unref(from);
    if (until)
        g_date_time_unref(until);
    if (!ok_from || !ok_until)
        admin_invalid(s, ok_from ? UNTIL : FROM,
                      _("Data inválida; use DD/MM/AAAA, como 05/11/2026."));
    else if (!ordered)
        admin_invalid(s, FROM, _("A data inicial é depois da final."));
    else if (!server_admin_audit_query(s->model, &q))
        admin_error(s, _("Filtro inválido."));
    else {
        s->dirty = false;
        admin_select(s, ROW_FILTERS);
    }
}
/* Enter in a field: picks the only match of the name search, else applies. */
static void submit(server_settings *s)
{
    static char query[TEXT_UTF8_BYTES];
    if (!*s->draft_actor && admin_text(&s->name, query) && *g_strstrip(query)) {
        char *folded = g_utf8_casefold(query, -1);
        json_t *found = NULL;
        int count = 0;
        for (size_t i = 0; folded && i < history_user_count(s->model); ++i)
            if (admin_user_matches(history_user(s->model, i), folded) && !count++)
                found = history_user(s->model, i);
        g_free(folded);
        if (count == 1) {
            snprintf(s->draft_actor, sizeof(s->draft_actor), "%s", admin_str(found, "id"));
            text_clear(&s->name);
            s->dirty = true;
            return;
        }
        if (count > 1) {
            admin_error(s, _("Escolha a pessoa na lista abaixo do campo."));
            return;
        }
    }
    apply(s);
}
static void cycle_action(server_settings *s, int direction)
{
    const json_t *actions = server_admin_audit_actions(s->model);
    int count = (int)json_array_size(actions), current = -1;
    for (int i = 0; i < count; ++i)
        if (!strcmp(json_string_value(json_array_get(actions, (size_t)i)), s->draft_action))
            current = i;
    /* -1 is Todas; an action no longer listed restarts from it. */
    int next = current + (direction < 0 ? -1 : 1);
    if (next < -1)
        next = count - 1;
    if (next >= count)
        next = -1;
    snprintf(s->draft_action, sizeof(s->draft_action), "%s",
             next < 0 ? "" : json_string_value(json_array_get(actions, (size_t)next)));
    if (!count)
        admin_error(s, _("Nenhuma ação carregada ainda para escolher."));
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    if (f->id == ENTITY) {
        s->choice = ((s->choice + (direction < 0 ? -1 : 1)) % ENTITIES + ENTITIES) % ENTITIES;
        s->dirty = true;
    } else if (f->id == ACTION) {
        cycle_action(s, direction);
        s->dirty = true;
    } else if (f->id == ACTOR) {
        snprintf(s->draft_actor, sizeof(s->draft_actor), "%.36s", f->key);
        text_clear(&s->name);
        s->dirty = true;
    } else if (f->id == ANYONE) {
        s->draft_actor[0] = 0;
        s->dirty = true;
    } else if (f->id == APPLY)
        apply(s);
    else if (f->id == RESET) {
        admin_audit_query none;
        memset(&none, 0, sizeof(none));
        server_admin_audit_query(s->model, &none);
        admin_select(s, ROW_FILTERS);
    } else if (f->id == RETRY)
        server_admin_load(s->model, ADMIN_LIST_AUDIT, false);
    else if (f->id == ONLY_ACTION || f->id == ONLY_ACTOR) {
        /* Narrows the filters in use by this event's action or author. */
        admin_audit_query q = *server_admin_audit_current(s->model);
        const json_t *log = selected_log(s);
        if (f->id == ONLY_ACTION)
            snprintf(q.action, sizeof(q.action), "%s", admin_str(log, "action"));
        else
            snprintf(q.actor, sizeof(q.actor), "%.36s", f->key);
        if (server_admin_audit_query(s->model, &q))
            admin_select(s, ROW_FILTERS);
        else
            admin_error(s, _("Filtro inválido."));
    }
}
const admin_tab_ops tab_audit = {
    .list = true,
    .empty = N_("Nenhum evento para mostrar."),
    .enter = enter,
    .rows = rows,
    .fill = fill,
    .fields = fields,
    .activate = activate,
    .submit = submit,
    .choose = choose,
};
