#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/input/editor.h"
#include "model/text/translation.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char channel[64];
    size_t cursor, length, anchor;
    bool selecting;
    int first_row, goal_column;
    bool trailing;
} cursor_state;

struct send_editor {
    auth_client *session;
    outbox *box;
    text_buffer text;
    cursor_state cursors[OUTBOX_DRAFT_LIMIT];
    char channel[64];
    uint64_t generation, revision;
    const char *notice;
    bool feedback, refused;
    const char *compact_notice;
    /* The confirmation notice is shown for confirmed_notice_us from the first
     * poll that sees that operation confirmed, then the hint returns. */
    uint64_t confirmed_id;
    gint64 confirmed_since, confirmed_notice_us;
};

/* Two seconds; PAPO_CONFIRMED_NOTICE_MS lets tests that inspect the notice
 * over a long sequence of steps keep it visible. */
static gint64 confirmed_notice_duration(void)
{
    const char *text = g_getenv("PAPO_CONFIRMED_NOTICE_MS");
    char *end = NULL;
    gint64 ms = text ? g_ascii_strtoll(text, &end, 10) : 0;
    return text && end != text && !*end && ms > 0 && ms <= 3600000 ? ms * 1000
                                                                    : 2 * G_USEC_PER_SEC;
}

static cursor_state *find_cursor(send_editor *editor, const char *channel)
{
    cursor_state *empty = NULL, *reusable = NULL;
    for (size_t i = 0; i < OUTBOX_DRAFT_LIMIT; ++i) {
        cursor_state *item = &editor->cursors[i];
        if (*item->channel && !strcmp(item->channel, channel))
            return item;
        if (!*item->channel && !empty)
            empty = item;
        if (*item->channel && strcmp(item->channel, editor->channel)) {
            const char *draft = outbox_draft(editor->box, item->channel, NULL);
            if (draft && !*draft && !reusable)
                reusable = item;
        }
    }
    if (!empty && reusable) {
        empty = reusable;
        memset(empty, 0, sizeof(*empty));
    }
    if (empty)
        memcpy(empty->channel, channel, strlen(channel) + 1);
    return empty;
}

static void save_cursor(send_editor *editor)
{
    cursor_state *item = find_cursor(editor, editor->channel);
    if (item) {
        item->cursor = editor->text.cursor;
        item->length = editor->text.length;
        item->anchor = editor->text.anchor;
        item->selecting = editor->text.selecting;
        item->first_row = editor->text.first_row;
        item->goal_column = editor->text.goal_column;
        item->trailing = editor->text.trailing;
    }
}

send_editor *send_editor_create(auth_client *session)
{
    if (!session)
        return NULL;
    send_editor *editor = calloc(1, sizeof(*editor));
    if (!editor)
        return NULL;
    editor->session = session;
    editor->confirmed_notice_us = confirmed_notice_duration();
    editor->box = outbox_create(session);
    if (!editor->box) {
        free(editor);
        return NULL;
    }
    editor->generation = auth_context_generation(session);
    editor->notice = _("Escreva uma mensagem; Enter envia.");
    text_clear(&editor->text);
    return editor;
}

void send_editor_destroy(send_editor *editor)
{
    if (!editor)
        return;
    outbox_destroy(editor->box);
    free(editor);
}

static void synchronize_generation(send_editor *editor)
{
    uint64_t generation = auth_context_generation(editor->session);
    if (generation == editor->generation)
        return;
    text_clear(&editor->text);
    memset(editor->cursors, 0, sizeof(editor->cursors));
    editor->channel[0] = '\0';
    editor->generation = generation;
    editor->notice = _("Confirme a sessão para continuar.");
    editor->feedback = false;
}

bool send_editor_changed(send_editor *editor)
{
    if (!editor)
        return false;
    synchronize_generation(editor);
    if (!*editor->channel)
        return false;
    char utf8[TEXT_UTF8_BYTES];
    if (!text_encode(&editor->text, utf8) || !outbox_set_draft(editor->box, editor->channel, utf8))
        return false;
    outbox_draft(editor->box, editor->channel, &editor->revision);
    save_cursor(editor);
    return true;
}

bool send_editor_select(send_editor *editor, const char *channel)
{
    if (!editor || !channel || !*channel || strlen(channel) >= sizeof(editor->channel))
        return false;
    synchronize_generation(editor);
    if (*editor->channel && !send_editor_changed(editor))
        return false;
    uint64_t revision;
    const char *draft = outbox_draft(editor->box, channel, &revision);
    if (!draft)
        return false;
    text_buffer next;
    if (!text_decode(&next, draft))
        return false;
    cursor_state *cursor = find_cursor(editor, channel);
    if (!cursor)
        return false;
    if (cursor->length >= next.length && cursor->length - next.length <= cursor->cursor)
        next.cursor = cursor->cursor - (cursor->length - next.length);
    else
        next.cursor = cursor->cursor < next.length ? cursor->cursor : next.length;
    size_t removed = cursor->length > next.length ? cursor->length - next.length : 0;
    next.anchor = cursor->anchor >= removed ? cursor->anchor - removed : 0;
    if (next.anchor > next.length)
        next.anchor = next.length;
    next.selecting = cursor->selecting;
    next.first_row = cursor->first_row;
    next.goal_column = cursor->goal_column;
    next.trailing = cursor->trailing;
    editor->text = next;
    memcpy(editor->channel, channel, strlen(channel) + 1);
    editor->revision = revision;
    editor->notice = _("Escreva uma mensagem; Enter envia.");
    editor->feedback = false;
    return true;
}

text_buffer *send_editor_input(send_editor *editor)
{
    if (!editor)
        return NULL;
    synchronize_generation(editor);
    return *editor->channel && auth_has_session(editor->session) ? &editor->text : NULL;
}

bool send_editor_refused(const send_editor *editor)
{
    return editor && editor->refused;
}
void send_editor_refuse(send_editor *editor, bool refused)
{
    if (editor)
        editor->refused = refused;
}

static void submit_notice(send_editor *editor, outbox_submit_result result)
{
    editor->feedback = true;
    editor->refused = result == OUTBOX_EMPTY;
    editor->compact_notice = result == OUTBOX_ACCEPTED              ? _("Aguardando envio")
                             : result == OUTBOX_EMPTY               ? _("Mensagem vazia")
                             : result == OUTBOX_BUSY                ? _("Envio em andamento")
                             : result == OUTBOX_CONFIRM_DUPLICATION ? _("Confirmar reenvio")
                             : result == OUTBOX_FORBIDDEN           ? _("Sem permissão")
                             : result == OUTBOX_PERMISSIONS_PENDING ? _("Confirmando permissões")
                             : result == OUTBOX_NO_SESSION          ? _("Confirme a sessão")
                                                                    : _("Falha no envio");
    switch (result) {
    case OUTBOX_ACCEPTED:
        editor->notice = _("Mensagem aguardando envio; você pode continuar digitando.");
        break;
    case OUTBOX_EMPTY:
        editor->notice = _("Escreva uma mensagem antes de enviar.");
        break;
    case OUTBOX_BUSY:
        editor->notice = _("Esta conversa já tem um envio em andamento.");
        break;
    case OUTBOX_CONFIRM_DUPLICATION:
        editor->notice = _("O envio anterior pode já existir. Confirme se deseja reenviar e "
                           "possivelmente duplicar.");
        break;
    case OUTBOX_NO_SESSION:
        editor->notice = _("Confirme a sessão antes de enviar.");
        break;
    case OUTBOX_FORBIDDEN:
        editor->notice = _("Sem permissão para enviar nesta conversa; rascunho preservado.");
        break;
    case OUTBOX_PERMISSIONS_PENDING:
        editor->notice =
            _("Permissões ainda não confirmadas. Aguarde ou use Ctrl+R; rascunho preservado.");
        break;
    case OUTBOX_FULL:
        editor->notice = _("Não há espaço para mais envios; revise os resultados pendentes.");
        break;
    default:
        editor->notice = _("Não foi possível preparar o envio; rascunho preservado.");
        break;
    }
}
static const char *compact_operation(const outbox_operation *operation)
{
    if (operation->state == OUTBOX_CONFIRMED)
        return _("Mensagem confirmada");
    if (operation->state == OUTBOX_SENDING)
        return _("Enviando mensagem");
    if (operation->state == OUTBOX_QUEUED)
        return _("Aguardando envio");
    if (operation->state == OUTBOX_UNCERTAIN)
        return _("Envio sem confirmação");
    if (operation->cancelled)
        return _("Envio cancelado");
    switch (operation->status) {
    case 400:
    case 413:
    case 415:
    case 422:
        return _("Mensagem recusada");
    case 401:
        return _("Envio não autorizado");
    case 403:
        return _("Sem permissão");
    case 404:
        return _("Conversa indisponível");
    case 429:
        return _("Limite de envios");
    default:
        return _("Envio falhou");
    }
}

outbox_submit_result send_editor_submit(send_editor *editor, bool accept_duplicate)
{
    if (!editor || !*editor->channel)
        return OUTBOX_INVALID;
    if (!send_editor_changed(editor)) {
        outbox_submit_result failure =
            auth_has_session(editor->session) ? OUTBOX_FULL : OUTBOX_NO_SESSION;
        submit_notice(editor, failure);
        return failure;
    }
    outbox_submit_result result =
        outbox_submit(editor->box, editor->channel, accept_duplicate, NULL);
    if (result == OUTBOX_ACCEPTED) {
        /* The accepted text left the draft for the optimistic history entry;
         * load the emptied draft before the next sync can write it back. */
        uint64_t revision;
        const char *draft = outbox_draft(editor->box, editor->channel, &revision);
        text_buffer next;
        if (draft && text_decode(&next, draft)) {
            editor->text = next;
            editor->revision = revision;
            save_cursor(editor);
        }
    }
    submit_notice(editor, result);
    return result;
}

outbox_submit_result send_editor_retry(send_editor *editor, uint64_t id, bool accept_duplicate)
{
    if (!editor)
        return OUTBOX_INVALID;
    send_editor_changed(editor);
    outbox_submit_result result = outbox_retry(editor->box, id, accept_duplicate, NULL);
    submit_notice(editor, result);
    return result;
}

void send_editor_poll(send_editor *editor)
{
    if (!editor)
        return;
    synchronize_generation(editor);
    if (*editor->channel)
        send_editor_changed(editor);
    outbox_poll(editor->box);
    if (!*editor->channel)
        return;
    uint64_t revision;
    const char *draft = outbox_draft(editor->box, editor->channel, &revision);
    if (draft && revision != editor->revision) {
        text_buffer next;
        if (text_decode(&next, draft)) {
            size_t removed =
                editor->text.length >= next.length ? editor->text.length - next.length : 0;
            next.cursor = editor->text.cursor >= removed ? editor->text.cursor - removed : 0;
            if (next.cursor > next.length)
                next.cursor = next.length;
            next.anchor = editor->text.anchor >= removed ? editor->text.anchor - removed : 0;
            if (next.anchor > next.length)
                next.anchor = next.length;
            next.selecting = editor->text.selecting;
            next.first_row = editor->text.first_row;
            next.goal_column = editor->text.goal_column;
            next.trailing = editor->text.trailing;
            editor->text = next;
            editor->revision = revision;
            save_cursor(editor);
        }
    }
    outbox_operation latest;
    if (outbox_latest(editor->box, editor->channel, &latest)) {
        if (latest.state == OUTBOX_CONFIRMED) {
            gint64 now = g_get_monotonic_time();
            if (latest.id != editor->confirmed_id) {
                editor->confirmed_id = latest.id;
                editor->confirmed_since = now;
            }
            if (now - editor->confirmed_since >= editor->confirmed_notice_us) {
                editor->notice = _("Escreva uma mensagem; Enter envia.");
                editor->feedback = false;
                return;
            }
        }
        editor->notice = outbox_operation_notice(&latest);
        editor->feedback = true;
        editor->compact_notice = compact_operation(&latest);
    }
}

const char *send_editor_notice(const send_editor *editor)
{
    return editor ? editor->notice : "";
}
const char *send_editor_feedback(const send_editor *editor)
{
    return editor && editor->feedback ? editor->notice : NULL;
}
const char *send_editor_compact_notice(const send_editor *editor)
{
    return editor && editor->feedback ? editor->compact_notice : NULL;
}
outbox *send_editor_outbox(send_editor *editor)
{
    return editor ? editor->box : NULL;
}
