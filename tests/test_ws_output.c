#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char received[256];
static size_t received_size;
static bool again, broken;
static CURLcode partial_send(CURL *easy, const void *data, size_t length, size_t *sent,
                             curl_off_t fragment, unsigned flags)
{
    (void)easy; (void)fragment;
    if (flags != CURLWS_TEXT) return CURLE_BAD_FUNCTION_ARGUMENT;
    if (broken) { *sent = 0; return CURLE_SEND_ERROR; }
    if (again) { again = false; *sent = 0; return CURLE_AGAIN; }
    *sent = length > 3 ? 3 : length;
    if (*sent > sizeof(received) - received_size) return CURLE_SEND_ERROR;
    memcpy(received + received_size, data, *sent); received_size += *sent;
    again = true;
    return CURLE_OK;
}
/* Force transport outcomes which loopback sockets cannot reliably produce. */
#define curl_ws_send partial_send
#include "../src/net/ws.c"
#undef curl_ws_send
#define CHECK(v) do { if (!(v)) { fprintf(stderr, "Check failed at %d\n", __LINE__); return 1; } } while (0)
int main(void)
{
    ws_client client = {.state = WS_OPEN};
    json_t *event = json_pack("{s:s}", "type", "heartbeat");
    CHECK(event && ws_send(&client, event)); json_decref(event);
    uint64_t now; CHECK(net_now_ms(&now));
    for (int i = 0; i < 40 && client.output_count; ++i) flush(&client, now);
    CHECK(!client.output_count && !client.output_bytes && !client.heartbeat_queued);
    CHECK(received_size == strlen("{\"type\":\"heartbeat\"}"));
    CHECK(!memcmp(received, "{\"type\":\"heartbeat\"}", received_size));
    CHECK(client.ack_deadline == now + 10000 && client.heartbeat_at == now + 30000);
    event = json_pack("{s:s}", "type", "heartbeat");
    CHECK(event && ws_send(&client, event)); json_decref(event);
    flush(&client, now + 10001);
    CHECK(client.state == WS_ERROR && !client.output_count && !client.output_bytes && !client.ack_deadline);
    client.state = WS_OPEN; broken = true;
    event = json_pack("{s:s}", "type", "heartbeat");
    CHECK(event && ws_send(&client, event)); json_decref(event);
    flush(&client, now);
    CHECK(client.state == WS_ERROR && !client.output_count);
    release(&client);
    return 0;
}
