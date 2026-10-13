#define _POSIX_C_SOURCE 200809L
#include "ui/send_editor.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char channel[64];
    size_t cursor, length;
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
};

static cursor_state *find_cursor(send_editor *editor, const char *channel)
{
    cursor_state *empty = NULL, *reusable = NULL;
    for (size_t i = 0; i < OUTBOX_DRAFT_LIMIT; ++i) {
        cursor_state *item = &editor->cursors[i];
        if (*item->channel && !strcmp(item->channel, channel)) return item;
        if (!*item->channel && !empty) empty = item;
        if (*item->channel && strcmp(item->channel, editor->channel)) {
            const char *draft = outbox_draft(editor->box, item->channel, NULL);
            if (draft && !*draft && !reusable) reusable = item;
        }
    }
    if (!empty && reusable) { empty = reusable; memset(empty, 0, sizeof(*empty)); }
    if (empty) memcpy(empty->channel, channel, strlen(channel) + 1);
    return empty;
}

static void save_cursor(send_editor *editor)
{
    cursor_state *item = find_cursor(editor, editor->channel);
    if (item) {
        item->cursor = editor->text.cursor; item->length = editor->text.length;
        item->first_row = editor->text.first_row; item->goal_column = editor->text.goal_column;
        item->trailing = editor->text.trailing;
    }
}

send_editor *send_editor_create(auth_client *session)
{
    if (!session) return NULL;
    send_editor *editor = calloc(1, sizeof(*editor));
    if (!editor) return NULL;
    editor->session = session; editor->box = outbox_create(session);
    if (!editor->box) { free(editor); return NULL; }
    editor->generation = auth_context_generation(session);
    editor->notice = "Escreva uma mensagem; Enter envia.";
    text_clear(&editor->text);
    return editor;
}

void send_editor_destroy(send_editor *editor)
{
    if (!editor) return;
    outbox_destroy(editor->box);
    free(editor);
}

static void synchronize_generation(send_editor *editor)
{
    uint64_t generation = auth_context_generation(editor->session);
    if (generation == editor->generation) return;
    text_clear(&editor->text);
    memset(editor->cursors, 0, sizeof(editor->cursors));
    editor->channel[0] = '\0';
    editor->generation = generation;
    editor->notice = "Confirme a sessão para continuar.";
}

bool send_editor_changed(send_editor *editor)
{
    if (!editor) return false;
    synchronize_generation(editor);
    if (!*editor->channel) return false;
    char utf8[TEXT_UTF8_BYTES];
    if (!text_encode(&editor->text, utf8) || !outbox_set_draft(editor->box, editor->channel, utf8)) return false;
    outbox_draft(editor->box, editor->channel, &editor->revision);
    save_cursor(editor);
    return true;
}

bool send_editor_select(send_editor *editor, const char *channel)
{
    if (!editor || !channel || !*channel || strlen(channel) >= sizeof(editor->channel)) return false;
    synchronize_generation(editor);
    if (*editor->channel && !send_editor_changed(editor)) return false;
    uint64_t revision;
    const char *draft = outbox_draft(editor->box, channel, &revision);
    if (!draft) return false;
    text_buffer next;
    if (!text_decode(&next, draft)) return false;
    cursor_state *cursor = find_cursor(editor, channel);
    if (!cursor) return false;
    if (cursor->length >= next.length && cursor->length - next.length <= cursor->cursor)
        next.cursor = cursor->cursor - (cursor->length - next.length);
    else next.cursor = cursor->cursor < next.length ? cursor->cursor : next.length;
    next.first_row = cursor->first_row; next.goal_column = cursor->goal_column; next.trailing = cursor->trailing;
    editor->text = next;
    memcpy(editor->channel, channel, strlen(channel) + 1);
    editor->revision = revision;
    editor->notice = "Escreva uma mensagem; Enter envia.";
    return true;
}

text_buffer *send_editor_input(send_editor *editor)
{
    if (!editor) return NULL;
    synchronize_generation(editor);
    return *editor->channel && auth_has_session(editor->session) ? &editor->text : NULL;
}

static void submit_notice(send_editor *editor, outbox_submit_result result)
{
    switch (result) {
        case OUTBOX_ACCEPTED: editor->notice = "Mensagem aguardando envio; você pode continuar digitando."; break;
        case OUTBOX_EMPTY: editor->notice = "Escreva uma mensagem antes de enviar."; break;
        case OUTBOX_BUSY: editor->notice = "Esta conversa já tem um envio em andamento."; break;
        case OUTBOX_CONFIRM_DUPLICATION: editor->notice = "O envio anterior pode já existir. Confirme se deseja reenviar e possivelmente duplicar."; break;
        case OUTBOX_NO_SESSION: editor->notice = "Confirme a sessão antes de enviar."; break;
        case OUTBOX_FULL: editor->notice = "Não há espaço para mais envios; revise os resultados pendentes."; break;
        default: editor->notice = "Não foi possível preparar o envio; rascunho preservado."; break;
    }
}

outbox_submit_result send_editor_submit(send_editor *editor, bool accept_duplicate)
{
    if (!editor || !*editor->channel) return OUTBOX_INVALID;
    if (!send_editor_changed(editor)) {
        outbox_submit_result failure = auth_has_session(editor->session) ? OUTBOX_FULL : OUTBOX_NO_SESSION;
        submit_notice(editor, failure); return failure;
    }
    outbox_submit_result result = outbox_submit(editor->box, editor->channel, accept_duplicate, NULL);
    submit_notice(editor, result);
    return result;
}

outbox_submit_result send_editor_retry(send_editor *editor, uint64_t id, bool accept_duplicate)
{
    if (!editor) return OUTBOX_INVALID;
    send_editor_changed(editor);
    outbox_submit_result result = outbox_retry(editor->box, id, accept_duplicate, NULL);
    submit_notice(editor, result);
    return result;
}

void send_editor_poll(send_editor *editor)
{
    if (!editor) return;
    synchronize_generation(editor);
    if (*editor->channel) send_editor_changed(editor);
    outbox_poll(editor->box);
    if (!*editor->channel) return;
    uint64_t revision;
    const char *draft = outbox_draft(editor->box, editor->channel, &revision);
    if (draft && revision != editor->revision) {
        text_buffer next;
        if (text_decode(&next, draft)) {
            size_t removed = editor->text.length >= next.length ? editor->text.length - next.length : 0;
            next.cursor = editor->text.cursor >= removed ? editor->text.cursor - removed : 0;
            if (next.cursor > next.length) next.cursor = next.length;
            next.first_row = editor->text.first_row;
            next.goal_column = editor->text.goal_column;
            next.trailing = editor->text.trailing;
            editor->text = next; editor->revision = revision;
            save_cursor(editor);
        }
    }
    outbox_operation latest;
    if (outbox_latest(editor->box, editor->channel, &latest)) editor->notice = outbox_operation_notice(&latest);
}

const char *send_editor_notice(const send_editor *editor) { return editor ? editor->notice : ""; }
outbox *send_editor_outbox(send_editor *editor) { return editor ? editor->box : NULL; }
