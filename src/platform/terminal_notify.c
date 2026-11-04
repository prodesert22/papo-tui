#include "platform/terminal_notify.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CLICK_LIMIT 16
#define FIELD_BYTES 2048 /* Protocol limit for one payload before Base64. */

static bool probed, ready;
/* Ids carry a random per-run prefix so a multiplexer or a stale reply from an
 * earlier run cannot be mistaken for one of this run's notifications. */
static unsigned session;
static unsigned clicked[CLICK_LIMIT];
static size_t clicked_count;

void terminal_notify_probe(void)
{
    if (probed)
        return;
    probed = true;
    const char *term = getenv("TERM");
    /* The Linux console prints unknown OSC strings instead of ignoring them. */
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || !term || !*term ||
        !strcmp(term, "dumb") || !strcmp(term, "linux"))
        return;
    session = g_random_int() | 1; /* Zero means "not probed". */
    fprintf(stdout, "\033]99;i=papo-%08x-probe:p=?;\033\\", session);
    fflush(stdout);
}

/* Finds key's value in a ':'-separated key=value list ending at end. */
static bool field(const char *start, const char *end, char key, const char **value, size_t *length)
{
    while (start < end) {
        const char *stop = memchr(start, ':', (size_t)(end - start));
        if (!stop)
            stop = end;
        if (stop - start >= 2 && start[0] == key && start[1] == '=') {
            *value = start + 2;
            *length = (size_t)(stop - start - 2);
            return true;
        }
        start = stop + 1;
    }
    return false;
}
static bool listed(const char *list, size_t length, const char *word)
{
    size_t size = strlen(word);
    for (const char *at = list, *end = list + length; at < end;) {
        const char *stop = memchr(at, ',', (size_t)(end - at));
        if (!stop)
            stop = end;
        if ((size_t)(stop - at) == size && !memcmp(at, word, size))
            return true;
        at = stop + 1;
    }
    return false;
}
bool terminal_notify_reply(const char *reply)
{
    if (!reply || strncmp(reply, "\033]99;", 5))
        return false;
    const char *metadata = reply + 5, *split = strchr(metadata, ';');
    if (!probed || !session || !split)
        return true;
    const char *payload = split + 1, *end = payload + strlen(payload);
    if (end > payload && end[-1] == '\a')
        --end;
    else if (end - payload >= 2 && end[-2] == '\033' && end[-1] == '\\')
        end -= 2;
    const char *id, *type = "";
    size_t id_length, type_length = 0;
    char prefix[32];
    int prefix_length = snprintf(prefix, sizeof(prefix), "papo-%08x-", session);
    if (!field(metadata, split, 'i', &id, &id_length) || id_length <= (size_t)prefix_length ||
        memcmp(id, prefix, (size_t)prefix_length))
        return true;
    field(metadata, split, 'p', &type, &type_length);
    id += prefix_length;
    id_length -= (size_t)prefix_length;
    if (type_length == 1 && *type == '?' && id_length == 5 && !memcmp(id, "probe", 5)) {
        const char *actions;
        size_t actions_length;
        ready = field(payload, end, 'a', &actions, &actions_length) &&
                listed(actions, actions_length, "focus") &&
                listed(actions, actions_length, "report");
        return true;
    }
    /* An activation has no payload type; a button press carries its number. */
    if (type_length || id_length > 9)
        return true;
    unsigned value = 0;
    for (size_t i = 0; i < id_length; ++i) {
        if (id[i] < '0' || id[i] > '9')
            return true;
        value = value * 10 + (unsigned)(id[i] - '0');
    }
    if (value && clicked_count < CLICK_LIMIT)
        clicked[clicked_count++] = value;
    return true;
}
bool terminal_notify_ready(void)
{
    return ready;
}
/* Writes one payload chunk: text cut at a UTF-8 boundary, controls as spaces,
 * Base64-encoded so nothing in it can end the escape sequence. */
static void chunk(unsigned id, const char *metadata, const char *type, const char *text)
{
    char clean[FIELD_BYTES];
    size_t length = strlen(text ? text : "");
    if (length > sizeof(clean)) {
        length = sizeof(clean);
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80)
            --length;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        clean[i] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
    }
    gchar *encoded = g_base64_encode((const guchar *)clean, length);
    fprintf(stdout, "\033]99;i=papo-%08x-%u:%se=1:p=%s;%s\033\\", session, id, metadata, type,
            encoded ? encoded : "");
    g_free(encoded);
}
bool terminal_notify_send(unsigned id, const char *title, const char *body)
{
    if (!ready || !id)
        return false;
    /* f is the Base64 application name, used by the terminal's own filters.
     * Delivery stays pending (d=0) until the body arrives. */
    chunk(id, "d=0:a=focus,report:o=always:u=1:f=UGFwbyBUVUk=:", "title", title);
    chunk(id, "d=1:", "body", body);
    fflush(stdout);
    return !ferror(stdout);
}
void terminal_notify_close(unsigned id)
{
    if (!ready || !id)
        return;
    fprintf(stdout, "\033]99;i=papo-%08x-%u:p=close;\033\\", session, id);
    fflush(stdout);
}
bool terminal_notify_take_activation(unsigned *id)
{
    if (!clicked_count)
        return false;
    *id = clicked[0];
    memmove(clicked, clicked + 1, --clicked_count * sizeof(*clicked));
    return true;
}
