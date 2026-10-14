#define _POSIX_C_SOURCE 200809L
#include "ui/media.h"
#include "ui/graphics.h"
#include "ui/theme.h"
#include "ui/text.h"
#include "ui/mouse.h"
#include <curl/curl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
static volatile sig_atomic_t stopping;
static void stop(int signum) { (void)signum; stopping = 1; }
int main(void)
{
    if (!setlocale(LC_ALL, "C.UTF-8") || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    auth_client *auth = auth_create();
    if (!auth) return 1;
    media_view *view = media_view_create(auth);
    if (!view) { auth_destroy(auth); curl_global_cleanup(); return 1; }
    signal(SIGINT, stop); signal(SIGTERM, stop);
    initscr(); raw(); noecho(); keypad(stdscr, true); timeout(30); mouseinterval(0);
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
    ui_theme theme; theme_init(&theme); init_graphemebreak();
    message_media item = {.kind = AUTH_MEDIA_ATTACHMENT, .size_known = true, .size_bytes = 43562,
        .source_width = 600, .source_height = 150};
    snprintf(item.id, sizeof(item.id), "00000000-0000-4000-8000-000000000901");
    snprintf(item.message, sizeof(item.message), "00000000-0000-4000-8000-000000000101");
    snprintf(item.label, sizeof(item.label), "sample-界.gif");
    snprintf(item.original_file_name, sizeof(item.original_file_name), "sample-界.gif");
    snprintf(item.mime, sizeof(item.mime), "image/gif");
    snprintf(item.moderation, sizeof(item.moderation), "clean\033]52;c;INJECT\a");
    snprintf(item.created_at, sizeof(item.created_at), "2026-10-14T15:51:18Z");
    int channel_clicks = 0;
    while (!stopping) {
        wbkgd(stdscr, COLOR_PAIR(PANEL)); erase();
        text_label(stdscr, 5, 1, 24, "Canais preservados");
        text_label(stdscr, 7, 1, 24, "# geral");
        text_label(stdscr, 8, 1, 24, "# outro");
        char status[80]; snprintf(status, sizeof(status), "%s · Canais acionados: %d", media_view_modal(view) ? "Modal aberto" : "Modal fechado", channel_clicks);
        text_label(stdscr, LINES - 1, 0, COLS, status);
        media_view_begin(view);
        media_view_offer(view, &item, 8, 28, 4, 3, 0, 0);
        wnoutrefresh(stdscr); media_view_draw(view); doupdate();
        wint_t key; int type = graphics_terminal_input(&key);
        if (type == ERR) continue;
        if (type == OK && (key == 17 || key == 3)) break;
        if (type == KEY_CODE_YES && key == KEY_RESIZE) continue;
        if (type == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT event;
            if (ui_mouse_get(&event) != OK) continue;
            if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
                media_view_scroll(view, event.y, event.x, event.bstate & BUTTON4_PRESSED ? -3 : 3);
            else if (event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
                bool consumed = media_view_click(view, event.y, event.x);
                if (!consumed && event.x < 25 && event.y >= 7 && event.y <= 8) ++channel_clicks;
            }
        } else media_view_key(view, type, key);
    }
    media_view_destroy(view); theme_restore(&theme); endwin();
    auth_destroy(auth); curl_global_cleanup(); return 0;
}
