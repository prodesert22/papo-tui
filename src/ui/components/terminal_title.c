#include "ui/components/terminal_title.h"
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Stay below the Windows console's 255-character title limit, including
 * when every UTF-8 byte represents an ASCII character. */
#define LABEL_BYTES 96
#define TITLE_BYTES (2 * LABEL_BYTES + 32)
static bool active;
static char current[TITLE_BYTES];

static void label_copy(char output[LABEL_BYTES + 1], const char *input)
{
    size_t used = 0;
    const char *p = input ? input : "";
    const char *start = p;
    while (*p && (size_t)(p - start) < 4096) {
        gunichar character = g_utf8_get_char_validated(p, -1);
        if (character == (gunichar)-1 || character == (gunichar)-2) {
            ++p;
            continue;
        }
        p = g_utf8_next_char(p);
        if (g_unichar_isspace(character))
            character = ' ';
        if (g_unichar_iscntrl(character))
            continue;
        char bytes[6];
        size_t length = (size_t)g_unichar_to_utf8(character, bytes);
        if (length > LABEL_BYTES - used)
            break;
        memcpy(output + used, bytes, length);
        used += length;
    }
    output[used] = '\0';
    g_strstrip(output);
}

static void publish(const char *title)
{
    if (!active || !strcmp(current, title))
        return;
    /* OSC 2 is shared by xterm-style terminals and Windows VT consoles. No
     * title queries are needed, so terminal replies cannot enter the editor. */
    if (fprintf(stdout, "\033]2;%s\033\\", title) >= 0 && fflush(stdout) == 0)
        snprintf(current, sizeof(current), "%s", title);
}

void terminal_title_reset(void)
{
    publish("Papo TUI");
}

void terminal_title_begin(void)
{
    if (active)
        return;
    const char *term = g_getenv("TERM");
    if (term && (!strcmp(term, "dumb") || !strcmp(term, "linux")))
        return;
    active = true;
    current[0] = '\0';
    /* Unsupported title-stack commands are ignored by the terminal. */
    fputs("\033[22;2t", stdout);
    terminal_title_reset();
}

void terminal_title_set(const char *conversation, const char *server, size_t mentions)
{
    if (!active)
        return;
    char safe_conversation[LABEL_BYTES + 1], safe_server[LABEL_BYTES + 1], title[TITLE_BYTES];
    label_copy(safe_conversation, conversation);
    label_copy(safe_server, server);
    if (!*safe_conversation)
        snprintf(safe_conversation, sizeof(safe_conversation), "%s", "Papo TUI");
    if (!*safe_server)
        snprintf(safe_server, sizeof(safe_server), "%s", "Servidor");
    if (mentions)
        snprintf(title, sizeof(title), "%s | %s (%zu)", safe_conversation, safe_server, mentions);
    else
        snprintf(title, sizeof(title), "%s | %s", safe_conversation, safe_server);
    publish(title);
}

void terminal_title_end(void)
{
    if (!active)
        return;
    /* Avoid leaving conversation names behind even without title-stack support. */
    terminal_title_reset();
    fputs("\033[23;2t", stdout);
    fflush(stdout);
    active = false;
    current[0] = '\0';
}
