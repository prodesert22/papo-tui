#include "net/auth/private.h"
#include <glib/gstdio.h>

static size_t file_read(char *data, size_t size, size_t count, void *context)
{
    FILE *file = context;
    if (size && count > SIZE_MAX / size)
        return CURL_READFUNC_ABORT;
    size_t n = fread(data, 1, size * count, file);
    return ferror(file) ? CURL_READFUNC_ABORT : n;
}
static int file_seek(void *context, curl_off_t offset, int origin)
{
    if (offset || origin != SEEK_SET)
        return CURL_SEEKFUNC_CANTSEEK;
    return fseek(context, 0, SEEK_SET) ? CURL_SEEKFUNC_FAIL : CURL_SEEKFUNC_OK;
}
static void file_close(void *context)
{
    fclose(context);
}

bool auth_internal_send_transmitted(auth_client *client)
{
    if (!client->attached)
        return false;
    long headers = 0;
    curl_off_t uploaded = 0;
    if (curl_easy_getinfo(client->easy, CURLINFO_REQUEST_SIZE, &headers) != CURLE_OK ||
        curl_easy_getinfo(client->easy, CURLINFO_SIZE_UPLOAD_T, &uploaded) != CURLE_OK)
        return true;
    return headers > 0 || uploaded > 0;
}

bool auth_send_text(auth_client *client, uint64_t operation_id, const char *channel_id,
                    const char *content)
{
    return auth_send_reply(client, operation_id, channel_id, content, NULL);
}

bool auth_send_reply(auth_client *client, uint64_t operation_id, const char *channel_id,
                     const char *content, const char *reply_to)
{
    return auth_send_attachments(client, operation_id, channel_id, content, reply_to, NULL, 0);
}

bool auth_send_attachments(auth_client *client, uint64_t operation_id, const char *channel_id,
                           const char *content, const char *reply_to, attachment_file *const *files,
                           size_t count)
{
    if (reply_to && *reply_to) {
        if (strlen(reply_to) != 36)
            return false;
        for (size_t i = 0; i < 36; ++i)
            if (i == 8 || i == 13 || i == 18 || i == 23
                    ? reply_to[i] != '-'
                    : !((reply_to[i] >= '0' && reply_to[i] <= '9') ||
                        (reply_to[i] >= 'a' && reply_to[i] <= 'f') ||
                        (reply_to[i] >= 'A' && reply_to[i] <= 'F')))
                return false;
    }
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued ||
        client->attached || client->api_path || client->response_ready || client->send_ready ||
        !operation_id || !channel_id || !*channel_id ||
        permissions_channel(client->permissions, channel_id, PERMISSION_SEND_MESSAGES) !=
            PERMISSION_ALLOWED ||
        strlen(channel_id) >= sizeof(client->send_channel) || !content || (!*content && !count) ||
        count > ATTACHMENT_LIMIT || (count && !files) || strlen(content) > 8192)
        return false;
    if (count &&
        permissions_role(client->permissions, PERMISSION_SEND_ATTACHMENT) != PERMISSION_ALLOWED)
        return false;
    json_t *valid_utf8 = json_string(content);
    if (!valid_utf8)
        return false;
    json_decref(valid_utf8);
    auth_internal_clear_send(client);
    client->send_has_attachments = count != 0;
    client->send_receipt = (auth_send_receipt){.outcome = AUTH_SEND_REJECTED,
                                               .operation_id = operation_id,
                                               .generation = client->generation};
    snprintf(client->send_channel, sizeof(client->send_channel), "%s", channel_id);
    if (!auth_internal_request(client, AUTH_MESSAGE_SEND, "/messages", NULL, true))
        return client->send_ready;
    /* Uploads can exceed the ordinary REST deadline, but stalled transfers
     * still finish and explicit cancellation keeps the existing receipt rules. */
    if (curl_easy_setopt(client->easy, CURLOPT_TIMEOUT_MS, count ? 0L : 15000L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_LOW_SPEED_LIMIT, count ? 1024L : 0L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_LOW_SPEED_TIME, count ? 30L : 0L) != CURLE_OK)
        goto error;
    client->mime = curl_mime_init(client->easy);
    if (!client->mime)
        goto error;
    curl_mimepart *channel = curl_mime_addpart(client->mime);
    curl_mimepart *text = curl_mime_addpart(client->mime);
    if (!channel || !text || curl_mime_name(channel, "channel_id") != CURLE_OK ||
        curl_mime_data(channel, channel_id, CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_mime_name(text, "content") != CURLE_OK ||
        curl_mime_data(text, content, CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_MIMEPOST, client->mime) != CURLE_OK)
        goto error;
    if (reply_to && *reply_to) {
        curl_mimepart *reply = curl_mime_addpart(client->mime);
        if (!reply || curl_mime_name(reply, "reply_to") != CURLE_OK ||
            curl_mime_data(reply, reply_to, CURL_ZERO_TERMINATED) != CURLE_OK)
            goto error;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!files[i])
            goto error;
        FILE *stream = g_fopen(attachment_path(files[i]), "rb");
        if (!stream)
            goto error;
        curl_mimepart *part = curl_mime_addpart(client->mime);
        if (!part || curl_mime_name(part, "attachments") != CURLE_OK ||
            curl_mime_filename(part, attachment_name(files[i])) != CURLE_OK ||
            curl_mime_type(part, attachment_mime(files[i])) != CURLE_OK ||
            curl_mime_data_cb(part, (curl_off_t)attachment_size(files[i]), file_read, file_seek,
                              file_close, stream) != CURLE_OK) {
            fclose(stream);
            goto error;
        }
    }
    return true;
error:
    auth_internal_detach(client);
    client->send_ready = true;
    client->state = AUTH_READY;
    return true; /* Accepted with a local failure; no request was transmitted. */
}

bool auth_send_result_ready(const auth_client *client)
{
    return client->send_ready;
}

json_t *auth_take_send_result(auth_client *client, auth_send_receipt *receipt)
{
    if (!client->send_ready)
        return NULL;
    if (receipt)
        *receipt = client->send_receipt;
    json_t *message = client->sent_message;
    client->sent_message = NULL;
    client->send_ready = false;
    return message;
}

bool auth_cancel_send(auth_client *client)
{
    if (client->operation != AUTH_MESSAGE_SEND || (!client->queued && !client->attached))
        return false;
    bool transmitted = auth_internal_send_transmitted(client);
    auth_internal_detach(client);
    client->send_receipt.transmitted = transmitted;
    client->send_receipt.cancelled = true;
    client->send_receipt.outcome = transmitted ? AUTH_SEND_UNCERTAIN : AUTH_SEND_REJECTED;
    client->send_ready = true;
    client->state = AUTH_READY;
    return true;
}
