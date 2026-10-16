#include "net/auth/private.h"

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
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued ||
        client->attached || client->api_path || client->response_ready || client->send_ready ||
        !operation_id || !channel_id || !*channel_id ||
        strlen(channel_id) >= sizeof(client->send_channel) || !content || !*content ||
        strlen(content) > 8192)
        return false;
    json_t *valid_utf8 = json_string(content);
    if (!valid_utf8)
        return false;
    json_decref(valid_utf8);
    auth_internal_clear_send(client);
    client->send_receipt = (auth_send_receipt){.outcome = AUTH_SEND_REJECTED,
                                               .operation_id = operation_id,
                                               .generation = client->generation};
    snprintf(client->send_channel, sizeof(client->send_channel), "%s", channel_id);
    if (!auth_internal_request(client, AUTH_MESSAGE_SEND, "/messages", NULL, true))
        return client->send_ready;
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
