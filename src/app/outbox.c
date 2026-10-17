#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "app/outbox.h"
#include "platform/unicode.h"

#include <graphemebreak.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

typedef struct {
    char channel[64];
    char reply[37];
    uint64_t reply_revision;
    char *text;
    uint64_t revision;
    GPtrArray *files;
} draft;
typedef struct {
    uint64_t id, generation, revision;
    outbox_state state;
    long status;
    char channel[64];
    char reply[37];
    uint64_t reply_revision;
    char *text;
    json_t *confirmed;
    GPtrArray *files;
    bool prefix_untouched, transmitted, cancelled;
} operation;

struct outbox {
    auth_client *session;
    char *instance, *account;
    uint64_t generation, next_id, active;
    draft drafts[OUTBOX_DRAFT_LIMIT];
    operation operations[OUTBOX_OPERATION_LIMIT];
};

static bool valid_channel(const char *value)
{
    if (!value || !*value || strlen(value) >= 64)
        return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        if (*p < 33 || *p >= 127)
            return false;
    return true;
}

static bool valid_text(const char *value, bool *nonempty)
{
    if (!value || strlen(value) >= OUTBOX_TEXT_BYTES)
        return false;
    json_t *utf8 = json_string(value);
    if (!utf8)
        return false;
    json_decref(utf8);
    size_t count = 0;
    bool content = false;
    while (*value) {
        uint32_t character;
        size_t bytes = papo_utf8_next(value, &character);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 ||
            (!g_unichar_isprint(character) && character != 0x200c && character != 0x200d &&
             character != '\n' && character != '\r' && character != '\t'))
            return false;
        if (++count > OUTBOX_TEXT_LIMIT)
            return false;
        content |= !g_unichar_isspace(character);
        value += bytes;
    }
    if (nonempty)
        *nonempty = content;
    return true;
}

static void dispose_operation(operation *item)
{
    free(item->text);
    g_clear_pointer(&item->files, g_ptr_array_unref);
    json_decref(item->confirmed);
    memset(item, 0, sizeof(*item));
}

void outbox_reset(outbox *box)
{
    if (!box)
        return;
    if (box->active && auth_context_generation(box->session) == box->generation)
        auth_cancel_send(box->session);
    for (size_t i = 0; i < OUTBOX_DRAFT_LIMIT; ++i) {
        free(box->drafts[i].text);
        g_clear_pointer(&box->drafts[i].files, g_ptr_array_unref);
        memset(&box->drafts[i], 0, sizeof(draft));
    }
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i)
        dispose_operation(&box->operations[i]);
    free(box->instance);
    free(box->account);
    box->instance = box->account = NULL;
    box->active = 0;
}

/* Never move an old operation into a newly authenticated generation. Frozen
 * drafts can be restored only after the same instance/account is verified. */
static bool synchronize(outbox *box)
{
    uint64_t generation = auth_context_generation(box->session);
    if (box->account && box->generation != generation) {
        for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i) {
            operation *item = &box->operations[i];
            if (!item->id)
                continue;
            if (item->state == OUTBOX_SENDING)
                item->state = OUTBOX_UNCERTAIN;
            else if (item->state == OUTBOX_QUEUED)
                item->state = OUTBOX_FAILED;
            json_decref(item->confirmed);
            item->confirmed = NULL;
        }
        box->active = 0;
        box->generation = generation;
    }
    if (!auth_has_session(box->session))
        return false;
    const char *instance = auth_instance_url(box->session), *account = auth_user_id(box->session);
    if (!*instance || !*account)
        return false;
    if (box->account && (!strcmp(instance, box->instance) && !strcmp(account, box->account)))
        return true;
    char *new_instance = strdup(instance), *new_account = strdup(account);
    if (!new_instance || !new_account) {
        free(new_instance);
        free(new_account);
        return false;
    }
    outbox_reset(box);
    box->instance = new_instance;
    box->account = new_account;
    box->generation = generation;
    return true;
}

outbox *outbox_create(auth_client *session)
{
    if (!session)
        return NULL;
    outbox *box = calloc(1, sizeof(*box));
    if (box) {
        box->session = session;
        init_graphemebreak();
    }
    return box;
}

void outbox_destroy(outbox *box)
{
    if (!box)
        return;
    outbox_reset(box);
    free(box);
}

static draft *find_draft(outbox *box, const char *channel, bool create)
{
    draft *empty = NULL;
    for (size_t i = 0; i < OUTBOX_DRAFT_LIMIT; ++i) {
        draft *item = &box->drafts[i];
        if (*item->channel && !strcmp(item->channel, channel))
            return item;
        if (!*item->channel && !empty)
            empty = item;
    }
    if (create && empty) {
        memcpy(empty->channel, channel, strlen(channel) + 1);
        return empty;
    }
    return NULL;
}

const char *outbox_draft(outbox *box, const char *channel, uint64_t *revision)
{
    if (!box || !valid_channel(channel) || !synchronize(box))
        return NULL;
    draft *item = find_draft(box, channel, false);
    if (revision)
        *revision = item ? item->revision : 0;
    return item && item->text ? item->text : "";
}

bool outbox_set_draft(outbox *box, const char *channel, const char *content)
{
    if (!box || !valid_channel(channel) || !valid_text(content, NULL) || !synchronize(box))
        return false;
    draft *item = find_draft(box, channel, false);
    if (!item && !*content)
        return true;
    if (item && !strcmp(item->text ? item->text : "", content))
        return true;
    if (item && item->revision == UINT64_MAX)
        return false;
    char *copy = strdup(content);
    if (!copy)
        return false;
    if (!item)
        item = find_draft(box, channel, true);
    if (!item) {
        free(copy);
        return false;
    }
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i) {
        operation *pending = &box->operations[i];
        if (pending->id && !strcmp(channel, pending->channel) &&
            (pending->state == OUTBOX_QUEUED || pending->state == OUTBOX_SENDING) &&
            pending->prefix_untouched &&
            strncmp(content, pending->text, strlen(pending->text)) != 0)
            pending->prefix_untouched = false;
    }
    free(item->text);
    item->text = copy;
    ++item->revision;
    return true;
}

static operation *find_operation(outbox *box, uint64_t id)
{
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i)
        if (box->operations[i].id == id && id)
            return &box->operations[i];
    return NULL;
}

bool outbox_set_reply(outbox *box, const char *channel, const char *reply)
{
    if (!box || !valid_channel(channel) || !synchronize(box) ||
        (reply && *reply && !g_uuid_string_is_valid(reply)))
        return false;
    draft *item = find_draft(box, channel, true);
    if (!item || item->reply_revision == UINT64_MAX)
        return false;
    snprintf(item->reply, sizeof(item->reply), "%s", reply ? reply : "");
    ++item->reply_revision;
    return true;
}

const char *outbox_reply(outbox *box, const char *channel)
{
    if (!box || !valid_channel(channel) || !synchronize(box))
        return "";
    draft *item = find_draft(box, channel, false);
    return item ? item->reply : "";
}

const GPtrArray *outbox_attachments(outbox *box, const char *channel)
{
    if (!box || !valid_channel(channel) || !synchronize(box))
        return NULL;
    draft *item = find_draft(box, channel, false);
    return item ? item->files : NULL;
}
bool outbox_add_attachment(outbox *box, const char *channel, attachment_file *file)
{
    if (!box || !file || !valid_channel(channel) || !synchronize(box))
        return false;
    draft *item = find_draft(box, channel, true);
    if (!item || (item->files && item->files->len >= ATTACHMENT_LIMIT))
        return false;
    if (!item->files)
        item->files = g_ptr_array_new_with_free_func((GDestroyNotify)attachment_unref);
    g_ptr_array_add(item->files, attachment_ref(file));
    return true;
}
bool outbox_remove_attachment(outbox *box, const char *channel, guint64 id)
{
    if (!box || !valid_channel(channel) || !synchronize(box))
        return false;
    draft *item = find_draft(box, channel, false);
    for (guint i = 0; item && item->files && i < item->files->len; ++i)
        if (attachment_id(g_ptr_array_index(item->files, i)) == id) {
            g_ptr_array_remove_index(item->files, i);
            return true;
        }
    return false;
}
static GPtrArray *snapshot_files(const GPtrArray *files)
{
    GPtrArray *copy = g_ptr_array_new_with_free_func((GDestroyNotify)attachment_unref);
    for (guint i = 0; files && i < files->len; ++i)
        g_ptr_array_add(copy, attachment_ref(g_ptr_array_index(files, i)));
    return copy;
}
static bool contains_files(const GPtrArray *draft_files, const GPtrArray *sent_files)
{
    for (guint i = 0; sent_files && i < sent_files->len; ++i) {
        bool found = false;
        for (guint j = 0; draft_files && j < draft_files->len; ++j)
            found |= attachment_id(g_ptr_array_index(draft_files, j)) ==
                     attachment_id(g_ptr_array_index(sent_files, i));
        if (!found)
            return false;
    }
    return true;
}

static outbox_submit_result enqueue(outbox *box, const char *channel, const char *content,
                                    const char *reply, const GPtrArray *files,
                                    bool accept_duplicate, uint64_t *id)
{
    bool nonempty;
    if (!valid_text(content, &nonempty))
        return OUTBOX_INVALID;
    if (!nonempty && (!files || !files->len))
        return OUTBOX_EMPTY;
    permission_decision access =
        permissions_channel(auth_permissions(box->session), channel, PERMISSION_SEND_MESSAGES);
    if (access != PERMISSION_ALLOWED)
        return access == PERMISSION_DENIED ? OUTBOX_FORBIDDEN : OUTBOX_PERMISSIONS_PENDING;
    operation *slot = NULL, *oldest = NULL;
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i) {
        operation *item = &box->operations[i];
        if (!item->id && !slot)
            slot = item;
        if (item->id && item->state == OUTBOX_CONFIRMED && !item->confirmed &&
            (!oldest || item->id < oldest->id))
            oldest = item;
        if (item->id && !strcmp(item->channel, channel)) {
            if (item->state == OUTBOX_QUEUED || item->state == OUTBOX_SENDING)
                return OUTBOX_BUSY;
            if (item->state == OUTBOX_UNCERTAIN && !accept_duplicate &&
                !strncmp(content, item->text, strlen(item->text)) &&
                contains_files(files, item->files))
                return OUTBOX_CONFIRM_DUPLICATION;
        }
    }
    if (!slot)
        slot = oldest;
    if (!slot || box->next_id == UINT64_MAX)
        return OUTBOX_FULL;
    char *snapshot = strdup(content);
    if (!snapshot)
        return OUTBOX_FULL;
    GPtrArray *file_copy = snapshot_files(files);
    draft *editor = find_draft(box, channel, false);
    if (slot->id)
        dispose_operation(slot);
    slot->id = ++box->next_id;
    slot->generation = box->generation;
    slot->revision = editor ? editor->revision : 0;
    snprintf(slot->reply, sizeof(slot->reply), "%s", reply ? reply : "");
    slot->reply_revision = editor ? editor->reply_revision : 0;
    slot->state = OUTBOX_QUEUED;
    memcpy(slot->channel, channel, strlen(channel) + 1);
    slot->text = snapshot;
    slot->files = file_copy;
    slot->prefix_untouched =
        editor && editor->text && !strncmp(editor->text, content, strlen(content));
    if (id)
        *id = slot->id;
    return OUTBOX_ACCEPTED;
}

outbox_submit_result outbox_submit(outbox *box, const char *channel, bool accept_duplicate,
                                   uint64_t *id)
{
    if (!box || !valid_channel(channel))
        return OUTBOX_INVALID;
    if (!synchronize(box))
        return OUTBOX_NO_SESSION;
    draft *item = find_draft(box, channel, false);
    return enqueue(box, channel, item && item->text ? item->text : "", item ? item->reply : "",
                   item ? item->files : NULL, accept_duplicate, id);
}

outbox_submit_result outbox_retry(outbox *box, uint64_t id, bool accept_duplicate, uint64_t *new_id)
{
    if (!box || !synchronize(box))
        return OUTBOX_NO_SESSION;
    operation *item = find_operation(box, id);
    if (!item)
        return OUTBOX_INVALID;
    if (item->state != OUTBOX_FAILED && item->state != OUTBOX_UNCERTAIN)
        return OUTBOX_BUSY;
    if (item->state == OUTBOX_UNCERTAIN && !accept_duplicate)
        return OUTBOX_CONFIRM_DUPLICATION;
    return enqueue(box, item->channel, item->text, item->reply, item->files, accept_duplicate,
                   new_id);
}

static void consume_confirmation(outbox *box, operation *item)
{
    draft *editor = find_draft(box, item->channel, false);
    if (!editor)
        return;
    /* IDs distinguish submitted files from later additions, even with equal names. */
    for (guint i = 0; item->files && i < item->files->len; ++i)
        outbox_remove_attachment(box, item->channel,
                                 attachment_id(g_ptr_array_index(item->files, i)));
    if (editor->reply_revision == item->reply_revision && !strcmp(editor->reply, item->reply)) {
        editor->reply[0] = 0;
        ++editor->reply_revision;
    }
    if (!editor->text || !item->prefix_untouched || editor->revision == UINT64_MAX)
        return;
    size_t length = strlen(item->text);
    if (strncmp(editor->text, item->text, length))
        return;
    char breaks[OUTBOX_TEXT_BYTES];
    set_graphemebreaks_utf8((const utf8_t *)editor->text, strlen(editor->text), NULL, breaks);
    if (length && breaks[length - 1] != GRAPHEMEBREAK_BREAK)
        return;
    memmove(editor->text, editor->text + length, strlen(editor->text + length) + 1);
    ++editor->revision;
}

void outbox_poll(outbox *box)
{
    if (!box)
        return;
    bool verified = synchronize(box);
    if (auth_send_result_ready(box->session)) {
        auth_send_receipt receipt;
        json_t *message = auth_take_send_result(box->session, &receipt);
        operation *item = find_operation(box, receipt.operation_id);
        if (item && receipt.generation == box->generation &&
            receipt.generation == item->generation) {
            item->state = receipt.outcome == AUTH_SEND_CONFIRMED   ? OUTBOX_CONFIRMED
                          : receipt.outcome == AUTH_SEND_UNCERTAIN ? OUTBOX_UNCERTAIN
                                                                   : OUTBOX_FAILED;
            item->status = receipt.status;
            item->transmitted = receipt.transmitted;
            item->cancelled = receipt.cancelled;
            if (item->state == OUTBOX_CONFIRMED) {
                item->confirmed = message;
                message = NULL;
                consume_confirmation(box, item);
            }
        }
        json_decref(message);
        box->active = 0;
    }
    if (!verified || box->active)
        return;
    operation *next = NULL;
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i) {
        operation *item = &box->operations[i];
        if (item->id && item->state == OUTBOX_QUEUED && (!next || item->id < next->id))
            next = item;
    }
    if (next && permissions_channel(auth_permissions(box->session), next->channel,
                                    PERMISSION_SEND_MESSAGES) != PERMISSION_ALLOWED) {
        next->state = OUTBOX_FAILED;
        next->status = 403;
        return;
    }
    if (next &&
        auth_send_attachments(box->session, next->id, next->channel, next->text, next->reply,
                              (attachment_file *const *)next->files->pdata, next->files->len)) {
        next->state = OUTBOX_SENDING;
        box->active = next->id;
    }
}

bool outbox_cancel(outbox *box, uint64_t id)
{
    if (!box || !synchronize(box))
        return false;
    operation *item = find_operation(box, id);
    if (!item)
        return false;
    if (item->state == OUTBOX_QUEUED) {
        item->state = OUTBOX_FAILED;
        item->cancelled = true;
        return true;
    }
    return item->state == OUTBOX_SENDING && box->active == id && auth_cancel_send(box->session);
}

static void view(const operation *item, outbox_operation *value)
{
    *value = (outbox_operation){.id = item->id,
                                .generation = item->generation,
                                .draft_revision = item->revision,
                                .state = item->state,
                                .status = item->status,
                                .channel_id = item->channel,
                                .content = item->text ? item->text : "",
                                .reply_to = item->reply,
                                .transmitted = item->transmitted,
                                .cancelled = item->cancelled};
}

bool outbox_get_operation(outbox *box, uint64_t id, outbox_operation *value)
{
    if (!box || !value || !synchronize(box))
        return false;
    operation *item = find_operation(box, id);
    if (!item)
        return false;
    view(item, value);
    return true;
}

bool outbox_latest(outbox *box, const char *channel, outbox_operation *value)
{
    if (!box || !value || !valid_channel(channel) || !synchronize(box))
        return false;
    operation *latest = NULL;
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i) {
        operation *item = &box->operations[i];
        if (item->id && !strcmp(channel, item->channel) && (!latest || item->id > latest->id))
            latest = item;
    }
    if (!latest)
        return false;
    view(latest, value);
    return true;
}

json_t *outbox_take_confirmed(outbox *box, outbox_operation *value)
{
    if (!box || !value || !synchronize(box))
        return NULL;
    operation *oldest = NULL;
    for (size_t i = 0; i < OUTBOX_OPERATION_LIMIT; ++i) {
        operation *item = &box->operations[i];
        if (item->confirmed && item->generation == box->generation &&
            (!oldest || item->id < oldest->id))
            oldest = item;
    }
    if (!oldest)
        return NULL;
    view(oldest, value);
    json_t *message = oldest->confirmed;
    oldest->confirmed = NULL;
    return message;
}

void outbox_reconciled(outbox *box, uint64_t id)
{
    operation *item = box ? find_operation(box, id) : NULL;
    if (item && item->state == OUTBOX_CONFIRMED && !item->confirmed) {
        free(item->text);
        item->text = NULL;
        g_clear_pointer(&item->files, g_ptr_array_unref);
    }
}

bool outbox_forget(outbox *box, uint64_t id)
{
    if (!box || !synchronize(box))
        return false;
    operation *item = find_operation(box, id);
    if (!item || item->state == OUTBOX_QUEUED || item->state == OUTBOX_SENDING || item->confirmed)
        return false;
    dispose_operation(item);
    return true;
}

const char *outbox_operation_notice(const outbox_operation *item)
{
    if (!item)
        return "";
    if (item->state == OUTBOX_QUEUED)
        return "Aguardando para enviar.";
    if (item->state == OUTBOX_SENDING)
        return "Enviando mensagem; você pode continuar digitando.";
    if (item->state == OUTBOX_CONFIRMED)
        return "Mensagem confirmada pelo servidor.";
    if (item->state == OUTBOX_UNCERTAIN)
        return "Envio sem confirmação. Pode já existir; reenviar pode duplicar.";
    if (item->cancelled)
        return "Envio cancelado; rascunho preservado.";
    switch (item->status) {
    case 400:
    case 413:
    case 415:
    case 422:
        return "Mensagem recusada. Confira o texto; rascunho preservado.";
    case 401:
        return "Envio não autorizado. Confirme a sessão antes de tentar novamente.";
    case 403:
        return "Sem permissão para enviar neste canal; rascunho preservado.";
    case 404:
        return "Conversa indisponível; rascunho preservado.";
    case 429:
        return "Limite de envios atingido. Aguarde e tente manualmente.";
    default:
        return "Não foi possível enviar; rascunho preservado.";
    }
}
