#define _POSIX_C_SOURCE 200809L
#include "ui/graphics.h"
#include "ui/mouse.h"
#include "ui/chat.h"
#include "ui/theme.h"
#include "ui/button.h"
#include "ui/text.h"
#include "net/clock.h"

#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wctype.h>

#define SERVERS 6
#define CHANNELS 8
#define LOCAL_MESSAGES 32
#define SIDEBAR_ROWS (CHANNELS + 3)
#define MIN_ROWS 14
#define MIN_COLS 44
#define WIDE_COLS 80
#define KEY_PASTE_START (KEY_MAX + 1)
#define KEY_PASTE_END (KEY_MAX + 2)
#define ANCHOR_HEADER SIZE_MAX
#define ANCHOR_GAP (SIZE_MAX - 1)

enum { TABS, CHANNEL_LIST, HISTORY, EDITOR, FOCUS_COUNT };
typedef enum { READY, LOADING, FAILED } demo_state;
typedef enum { NO_MODAL, HELP_MODAL, NAV_MODAL, INFO_MODAL, EXIT_MODAL } modal_type;

typedef struct {
    const char *name;
    const char *channels[CHANNELS];
    const char *topic;
} demo_server;

/* Synthetic fixtures only. Each server has its own conversations and drafts. */
static const demo_server servers[SERVERS] = {
    {"Comunidade", {"geral", "projetos", "ajuda", "ideias", "eventos", "off-topic", "Ana", "Bruno"}, "Boas-vindas à comunidade"},
    {"Trabalho", {"equipe", "planejamento", "entregas", "suporte", "reuniões", "avisos", "Carla", "Diego"}, "Conversas da equipe"},
    {"Estudos", {"biblioteca", "exercícios", "dúvidas", "leituras", "pesquisa", "agenda", "Eva", "Fábio"}, "Aprender e compartilhar"},
    {"Projetos de código aberto", {"desenvolvimento", "revisões", "bugs", "documentação", "releases", "ideias", "Gabi", "Hugo"}, "Projetos e contribuições"},
    {"Jogos", {"partidas", "estratégias", "novidades", "grupos", "eventos", "conversa", "Iara", "João"}, "Encontre seu grupo"},
    {"Laboratório 界", {"experimentos", "resultados", "discussões", "artigos", "ferramentas", "agenda", "Lia", "Marco"}, "Acentos e caracteres largos: ação 界語"},
};

static const char *messages[] = {
    "10:32 Ana: Olá! Esta conversa contém somente mensagens simuladas.",
    "10:33 Você: Bom dia! As abas no topo representam servidores independentes.",
    "10:34 Bruno: Os canais ficam na coluna da esquerda. O histórico ocupa o painel central.",
    "10:35 Ana: Uma mensagem longa deve se ajustar à largura disponível sem atravessar a borda ou cobrir a lista de canais. Redimensione o terminal para observar a organização dos painéis.",
    "10:36 Você: Texto UTF-8: ação, café, reunião e caracteres largos 界語.",
    "10:37 Bruno: PageUp e PageDown percorrem o histórico. O rascunho pertence a esta conversa.",
    "10:38 Ana: Em um terminal estreito, Tab alterna canais e conversa em uma tecla.",
    "10:39 Você: Você pode mudar de servidor e voltar para continuar seu rascunho.",
    "10:40 Bruno: Ctrl+N insere uma quebra de linha; Enter envia somente na demonstração.",
    "10:41 Autor desconhecido: A mensagem permanece legível sem o perfil do autor.",
    "10:42 Ana: Emojis simples: 🙂 🚀. Compostos: 👨‍👩‍👧‍👦 🇧🇷. Sequências não representáveis aparecem como ?, mantendo o texto original.",
    "10:43 Você: Duas linhas de texto:\nesta é a segunda linha.",
    "10:44 Ana: Acento combinado: cafe\u0301.",
};


typedef struct {
    char *body; /* Owned UTF-8 allocation. */
    bool mine;
} local_message;

typedef struct {
    text_buffer draft;
    local_message messages[LOCAL_MESSAGES];
    int message_count, anchor_message;
    size_t anchor_offset;
    bool at_end;
    unsigned int unseen;
    demo_state state;
    uint64_t loading_until;
} conversation;

typedef struct {
    int channel, channel_cursor, first_row;
    conversation conversations[CHANNELS];
} instance;

typedef struct { int x, width; } click_range;
typedef struct { int server, channel; } nav_result;

typedef struct {
    /* Windows belong to the UI; model buffers survive reconstruction. */
    WINDOW *frame, *tabs, *sidebar, *conversation, *input, *footer, *modal;
    instance instances[SERVERS];
    int server, tab_cursor, first_tab, focus;
    int content_rows, content_width, sidebar_width, editor_rows, editor_border, history_height;
    click_range tab_hits[SERVERS + 1], tabs_back, tabs_next, channels_link, conversation_link;
    click_range history_end, retry, editor_label;
    int channel_hits[SIDEBAR_ROWS];
    bool narrow, show_channels, paste, paste_cr, paste_full;
    const char *notice;
    modal_type modal_type;
    int modal_width, modal_height, modal_choice, help_first;
    text_buffer query;
    nav_result results[SERVERS * CHANNELS];
    int result_count, result_cursor, first_result, nav_focus;
} chat_ui;

static volatile sig_atomic_t interrupted;
static void on_signal(int number) { (void)number; interrupted = 1; }
static int put_text(WINDOW *window, int y, int x, int width, const char *value)
{
    return text_label(window, y, x, width, value);
}

static conversation *current(chat_ui *ui)
{
    return &ui->instances[ui->server].conversations[ui->instances[ui->server].channel];
}

static bool panel_focused(const chat_ui *ui, int focus)
{
    return ui->modal_type == NO_MODAL && ui->focus == focus;
}

static void close_modal(chat_ui *ui)
{
    if (ui->modal) delwin(ui->modal);
    ui->modal = NULL;
    ui->modal_type = NO_MODAL;
}

static bool create_modal(chat_ui *ui)
{
    if (ui->modal) delwin(ui->modal);
    ui->modal = NULL;
    if (ui->modal_type == NO_MODAL || !ui->frame) return true;
    ui->modal_width = COLS > 76 ? 72 : COLS - 4;
    ui->modal_height = LINES > 24 ? 20 : LINES - 4;
    ui->modal = newwin(ui->modal_height, ui->modal_width,
                      (LINES - ui->modal_height) / 2, (COLS - ui->modal_width) / 2);
    if (!ui->modal) return false;
    wbkgd(ui->modal, COLOR_PAIR(PANEL));
    return true;
}

static bool open_modal(chat_ui *ui, modal_type type)
{
    close_modal(ui);
    ui->modal_type = type;
    ui->notice = NULL;
    ui->modal_choice = 1;
    if (type == HELP_MODAL) ui->help_first = 0;
    if (type == NAV_MODAL) {
        text_clear(&ui->query);
        ui->result_cursor = ui->first_result = ui->nav_focus = 0;
    }
    return create_modal(ui);
}

static void destroy_layout(chat_ui *ui)
{
    if (ui->modal) delwin(ui->modal);
    ui->modal = NULL;
    if (ui->input) delwin(ui->input);
    if (ui->conversation) delwin(ui->conversation);
    if (ui->sidebar) delwin(ui->sidebar);
    if (ui->footer) delwin(ui->footer);
    if (ui->tabs) delwin(ui->tabs);
    if (ui->frame) delwin(ui->frame);
    ui->input = ui->conversation = ui->sidebar = ui->footer = ui->tabs = ui->frame = NULL;
}

static bool layout(chat_ui *ui)
{
    curs_set(0);
    destroy_layout(ui);
    erase();
    refresh();
    if (LINES < MIN_ROWS || COLS < MIN_COLS) return true;
    ui->narrow = COLS < WIDE_COLS;
    ui->content_rows = LINES - 6;
    ui->sidebar_width = ui->narrow ? COLS - 2 : 24;
    ui->content_width = ui->narrow ? COLS - 2 : COLS - 3 - ui->sidebar_width;
    ui->editor_rows = ui->content_rows >= 10 ? 3 : 2;
    ui->editor_border = ui->content_rows - ui->editor_rows - 2;
    ui->history_height = ui->editor_border - 3;
    ui->frame = newwin(LINES, COLS, 0, 0);
    if (!ui->frame) return false;
    wbkgd(ui->frame, COLOR_PAIR(NORMAL));
    ui->tabs = derwin(ui->frame, 2, COLS - 2, 1, 1);
    ui->footer = derwin(ui->frame, 2, COLS - 2, LINES - 3, 1);
    if (!ui->tabs || !ui->footer) return false;
    wbkgd(ui->tabs, COLOR_PAIR(PANEL));
    wbkgd(ui->footer, COLOR_PAIR(PANEL));
    if (!ui->narrow || ui->show_channels) {
        ui->sidebar = derwin(ui->frame, ui->content_rows, ui->sidebar_width, 3, 1);
        if (!ui->sidebar) return false;
        wbkgd(ui->sidebar, COLOR_PAIR(PANEL));
    }
    if (!ui->narrow || !ui->show_channels) {
        ui->conversation = derwin(ui->frame, ui->content_rows, ui->content_width,
                                 3, ui->narrow ? 1 : ui->sidebar_width + 2);
        if (!ui->conversation) return false;
        ui->input = derwin(ui->conversation, ui->editor_rows, ui->content_width - 6,
                          ui->editor_border + 1, 3);
        if (!ui->input) return false;
        wbkgd(ui->input, COLOR_PAIR(INPUT));
    }
    return create_modal(ui);
}
static int tab_width(int index)
{
    if (index == SERVERS) return 15;
    return put_text(NULL, 0, 0, 1000, servers[index].name) + 4;
}

static void draw_tabs(chat_ui *ui)
{
    const int width = COLS - 2;
    bool active = panel_focused(ui, TABS);
    wbkgd(ui->tabs, COLOR_PAIR(active ? FOCUS_AREA : PANEL));
    werase(ui->tabs);
    wattrset(ui->tabs, COLOR_PAIR(PANEL_ACCENT));
    memset(ui->tab_hits, 0, sizeof(ui->tab_hits));
    ui->tabs_back = ui->tabs_next = (click_range){0};
    ui->channels_link = ui->conversation_link = (click_range){0};
    if (ui->tab_cursor < ui->first_tab) ui->first_tab = ui->tab_cursor;
    for (;;) {
        int used = ui->first_tab ? 2 : 0;
        for (int i = ui->first_tab; i <= ui->tab_cursor; ++i) used += tab_width(i);
        if (used <= width - 2 || ui->first_tab == ui->tab_cursor) break;
        ++ui->first_tab;
    }
    int x = ui->first_tab ? 2 : 0;
    if (ui->first_tab) {
        put_text(ui->tabs, 0, 0, width, "‹");
        ui->tabs_back = (click_range){0, 1};
    }
    for (int i = ui->first_tab; i <= SERVERS; ++i) {
        int size = tab_width(i);
        if (x + size > width - 2) {
            put_text(ui->tabs, 0, width - 2, 1, "›");
            ui->tabs_next = (click_range){width - 2, 1};
            break;
        }
        bool focused = active && i == ui->tab_cursor;
        wattrset(ui->tabs, COLOR_PAIR(focused ? FOCUSED : i == ui->server ? PANEL_ACTIVE : active ? FOCUS_MUTED : PANEL_MUTED) |
                 (focused || i == ui->server ? A_BOLD : A_NORMAL));
        char label[160];
        snprintf(label, sizeof(label), "%c%s%c", i == ui->server ? '[' : focused ? '>' : ' ',
                 i == SERVERS ? "+ Adicionar" : servers[i].name, i == ui->server ? ']' : focused ? '<' : ' ');
        int painted = i == SERVERS ? button_draw(ui->tabs, 0, x, size, label,
                      focused ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL) : put_text(ui->tabs, 0, x, size, label);
        ui->tab_hits[i] = (click_range){x, painted};
        x += size;
    }
    wattrset(ui->tabs, COLOR_PAIR(active ? FOCUS_MUTED : PANEL_MUTED));
    char summary[200];
    if (ui->narrow) {
        snprintf(summary, sizeof(summary), "DEMO · %s", servers[ui->server].name);
        put_text(ui->tabs, 1, 0, width - 20, summary);
        ui->channels_link = (click_range){width - 19, 8};
        ui->conversation_link = (click_range){width - 10, 10};
        wattrset(ui->tabs, COLOR_PAIR(ui->show_channels ? (active ? FOCUSED : PANEL_ACCENT) : active ? FOCUS_MUTED : PANEL_MUTED));
        put_text(ui->tabs, 1, ui->channels_link.x, 8, "[Canais]");
        wattrset(ui->tabs, COLOR_PAIR(ui->show_channels ? (active ? FOCUS_MUTED : PANEL_MUTED) : active ? FOCUSED : PANEL_ACCENT));
        put_text(ui->tabs, 1, ui->conversation_link.x, 10, "[Conversa]");
    } else {
        snprintf(summary, sizeof(summary), "DEMONSTRAÇÃO · %s · dados simulados", servers[ui->server].name);
        put_text(ui->tabs, 1, 0, width, summary);
    }
}


static int channel_row(int channel) { return channel + 1 + (channel >= 3) + (channel >= 6); }

static void draw_channels(chat_ui *ui)
{
    for (int i = 0; i < SIDEBAR_ROWS; ++i) ui->channel_hits[i] = -1;
    if (!ui->sidebar) return;
    bool active = panel_focused(ui, CHANNEL_LIST);
    wbkgd(ui->sidebar, COLOR_PAIR(active ? FOCUS_AREA : PANEL));
    werase(ui->sidebar);
    instance *state = &ui->instances[ui->server];
    int visible = ui->content_rows - 2;
    int selected = channel_row(state->channel_cursor);
    if (selected < state->first_row) state->first_row = selected;
    if (selected >= state->first_row + visible) state->first_row = selected - visible + 1;
    int last = SIDEBAR_ROWS > visible ? SIDEBAR_ROWS - visible : 0;
    if (state->first_row > last) state->first_row = last;
    wattrset(ui->sidebar, COLOR_PAIR(active ? FOCUS_MUTED : PANEL_MUTED) | A_BOLD);
    put_text(ui->sidebar, 0, 1, ui->sidebar_width - 2, "Canais / DMs");
    for (int item = state->first_row, row = 1; item < SIDEBAR_ROWS && row <= visible; ++item, ++row) {
        int channel = -1;
        for (int i = 0; i < CHANNELS; ++i) if (channel_row(i) == item) channel = i;
        if (channel < 0) {
            wattrset(ui->sidebar, COLOR_PAIR(active ? FOCUS_MUTED : PANEL_MUTED) | A_BOLD);
            put_text(ui->sidebar, row, 1, ui->sidebar_width - 2,
                     item == 0 ? "CONVERSAS" : item == 4 ? "PROJETOS" : "MENSAGENS DIRETAS");
            continue;
        }
        ui->channel_hits[row - 1] = channel;
        bool focused = active && channel == state->channel_cursor;
        wattrset(ui->sidebar, COLOR_PAIR(focused ? FOCUSED : channel == state->channel ? PANEL_ACTIVE : active ? FOCUS_AREA : PANEL) |
                 (focused || channel == state->channel ? A_BOLD : A_NORMAL));
        mvwhline(ui->sidebar, row, 1, ' ', ui->sidebar_width - 2);
        char label[100];
        snprintf(label, sizeof(label), "%c %s %s", channel == state->channel ? '>' : focused ? '*' : ' ',
                 channel < 6 ? "#" : "@", servers[ui->server].channels[channel]);
        put_text(ui->sidebar, row, 1, ui->sidebar_width - 2, label);
    }
    wattrset(ui->sidebar, COLOR_PAIR(active ? FOCUS_MUTED : PANEL_MUTED));
    put_text(ui->sidebar, ui->content_rows - 1, 1, ui->sidebar_width - 2, "↑↓ escolher · Enter abrir");
}

static int fixture_count(chat_ui *ui)
{
    return ui->instances[ui->server].channel == 2 ? 0 : (int)(sizeof(messages) / sizeof(messages[0]));
}

typedef struct { int total, anchor; } history_info;

/* The viewport refers to a message and grapheme offset, rather than a wrapped
 * screen row. New messages and resize preserve this stable reading anchor. */
static history_info history_walk(chat_ui *ui, int first, bool paint, bool save_anchor)
{
    conversation *state = current(ui);
    history_info info = {0};
    int fixtures = fixture_count(ui);
    for (int i = 0; i < fixtures + state->message_count; ++i) {
        char label[100];
        const char *body;
        if (i < fixtures) {
            const char *colon = strchr(messages[i], ':');
            /* The first colon is in the timestamp; the second ends the author. */
            if (colon) colon = strchr(colon + 1, ':');
            if (!colon) { snprintf(label, sizeof(label), "Autor desconhecido"); body = messages[i]; }
            else {
                size_t size = (size_t)(colon - messages[i]);
                if (size >= sizeof(label)) size = sizeof(label) - 1;
                memcpy(label, messages[i], size);
                label[size] = '\0';
                body = colon + 1;
                if (*body == ' ') ++body;
            }
        } else {
            local_message *message = &state->messages[i - fixtures];
            snprintf(label, sizeof(label), "Agora · %s · simulado", message->mine ? "Você" : "Ana");
            body = message->body;
        }
        int header = info.total++;
        if (state->anchor_message == i && state->anchor_offset == ANCHOR_HEADER) info.anchor = header;
        if (save_anchor && first == header) { state->anchor_message = i; state->anchor_offset = ANCHOR_HEADER; }
        if (paint && header >= first && header < first + ui->history_height) {
            wattrset(ui->conversation, COLOR_PAIR(panel_focused(ui, HISTORY) ? HISTORY_ACCENT : ACCENT));
            put_text(ui->conversation, header - first + 3, 2, ui->content_width - 4, label);
        }
        text_buffer buffer;
        text_layout lines;
        if (!text_decode(&buffer, body)) text_decode(&buffer, "Texto indisponível.");
        text_measure(&buffer, ui->content_width - 4, &lines);
        if (state->anchor_message == i && state->anchor_offset < ANCHOR_GAP) {
            for (size_t k = 0; k < lines.count; ++k)
                if (lines.cells[k].start <= state->anchor_offset) info.anchor = info.total + lines.cells[k].row;
        }
        if (save_anchor && first >= info.total && first < info.total + lines.rows) {
            for (size_t k = 0; k < lines.count; ++k) {
                if (lines.cells[k].row != first - info.total) continue;
                state->anchor_message = i;
                state->anchor_offset = lines.cells[k].start;
                break;
            }
        }
        if (paint) {
            wattrset(ui->conversation, COLOR_PAIR(panel_focused(ui, HISTORY) ? HISTORY_AREA : NORMAL));
            text_draw(ui->conversation, &buffer, &lines, first - info.total, ui->history_height, 3, 2);
        }
        info.total += lines.rows;
        if (state->anchor_message == i && state->anchor_offset == ANCHOR_GAP) info.anchor = info.total;
        if (save_anchor && first == info.total) { state->anchor_message = i; state->anchor_offset = ANCHOR_GAP; }
        ++info.total;
    }
    return info;
}

static int history_first(chat_ui *ui, history_info info)
{
    int last = info.total > ui->history_height ? info.total - ui->history_height : 0;
    return current(ui)->at_end ? last : info.anchor > last ? last : info.anchor;
}

static void history_move(chat_ui *ui, int delta)
{
    conversation *state = current(ui);
    if (!ui->conversation || state->state != READY) return;
    history_info info = history_walk(ui, 0, false, false);
    int first = history_first(ui, info) + delta;
    int last = info.total > ui->history_height ? info.total - ui->history_height : 0;
    if (first < 0) first = 0;
    if (first > last) first = last;
    state->at_end = first == last;
    history_walk(ui, first, false, true);
    if (state->at_end) state->unseen = 0;
}

static void draw_conversation(chat_ui *ui)
{
    ui->history_end = ui->retry = ui->editor_label = (click_range){0};
    if (!ui->conversation) return;
    bool history_active = panel_focused(ui, HISTORY);
    bool editor_active = panel_focused(ui, EDITOR);
    /* Focus only changes the region's background; foregrounds, geometry and
     * neutral separators stay consistent with the unfocused presentation. */
    werase(ui->conversation);
    if (history_active) {
        wattrset(ui->conversation, COLOR_PAIR(HISTORY_AREA));
        for (int row = 0; row < ui->editor_border; ++row)
            mvwhline(ui->conversation, row, 0, ' ', ui->content_width);
    }
    if (editor_active) {
        wattrset(ui->conversation, COLOR_PAIR(FOCUS_AREA));
        for (int row = ui->editor_border; row < ui->content_rows; ++row)
            mvwhline(ui->conversation, row, 0, ' ', ui->content_width);
    }
    conversation *state = current(ui);
    int channel = ui->instances[ui->server].channel;
    char title[140];
    snprintf(title, sizeof(title), "%s %s · Histórico", channel < 6 ? "#" : "@", servers[ui->server].channels[channel]);
    int title_width = ui->content_width - 4;
    if (state->unseen) {
        char badge[40];
        snprintf(badge, sizeof(badge), "[%u novas · Fim]", state->unseen);
        int width = put_text(NULL, 0, 0, 100, badge);
        ui->history_end = (click_range){ui->content_width - width - 2, width};
        title_width = ui->history_end.x - 3;
        wattrset(ui->conversation, COLOR_PAIR(history_active ? HISTORY_ACCENT : ACCENT) | A_BOLD);
        put_text(ui->conversation, 0, ui->history_end.x, width, badge);
    }
    wattrset(ui->conversation, COLOR_PAIR(history_active ? HISTORY_MUTED : MUTED) | A_BOLD);
    put_text(ui->conversation, 0, 2, title_width, title);
    wattrset(ui->conversation, COLOR_PAIR(state->state == FAILED ? (history_active ? HISTORY_ERROR : ERROR) : history_active ? HISTORY_MUTED : MUTED));
    put_text(ui->conversation, 1, 2, ui->content_width - 4,
             state->state == FAILED ? "Erro simulado ao carregar mensagens." : servers[ui->server].topic);
    wattrset(ui->conversation, COLOR_PAIR(history_active ? HISTORY_BORDER : BORDER));
    mvwhline(ui->conversation, 2, 0, ACS_HLINE, ui->content_width);
    if (state->state == LOADING) {
        wattrset(ui->conversation, COLOR_PAIR(history_active ? HISTORY_MUTED : MUTED));
        put_text(ui->conversation, 3, 2, ui->content_width - 4, "Carregando dados simulados…");
    } else if (state->state == FAILED) {
        wattrset(ui->conversation, COLOR_PAIR(history_active ? HISTORY_ACCENT : ACCENT) | A_BOLD);
        int width = put_text(ui->conversation, 3, 2, ui->content_width - 4, "[Tentar novamente · Ctrl+R]");
        ui->retry = (click_range){2, width};
    } else if (!fixture_count(ui) && !state->message_count) {
        wattrset(ui->conversation, COLOR_PAIR(history_active ? HISTORY_MUTED : MUTED));
        put_text(ui->conversation, 3, 2, ui->content_width - 4, "Nenhuma mensagem. Escreva no editor.");
    } else {
        history_info info = history_walk(ui, 0, false, false);
        history_walk(ui, history_first(ui, info), true, false);
        if (state->at_end) state->unseen = 0;
    }
    wattrset(ui->conversation, COLOR_PAIR(editor_active ? FOCUS_BORDER : BORDER));
    mvwhline(ui->conversation, ui->editor_border, 0, ACS_HLINE, ui->content_width);
    wattrset(ui->conversation, COLOR_PAIR(editor_active ? FOCUS_MUTED : MUTED));
    ui->editor_label = (click_range){2,
        put_text(ui->conversation, ui->editor_border, 2, ui->content_width - 4,
                 " Editor ")};
    wattrset(ui->conversation, COLOR_PAIR(editor_active ? FOCUS_MUTED : MUTED));
    put_text(ui->conversation, ui->editor_border + 1, 1, 1, ">");
    wattrset(ui->conversation, COLOR_PAIR(editor_active ? FOCUS_MUTED : MUTED));
    put_text(ui->conversation, ui->content_rows - 1, 2, ui->content_width - 4, "Enter: enviar · Ctrl+N: nova linha");
    chtype attributes = COLOR_PAIR(editor_active ? FOCUS_AREA : INPUT);
    wbkgd(ui->input, attributes);
    werase(ui->input);
    wattrset(ui->input, attributes);
    text_layout editor;
    text_measure(&state->draft, ui->content_width - 6, &editor);
    text_keep_cursor(&state->draft, &editor, ui->editor_rows);
    text_draw(ui->input, &state->draft, &editor, state->draft.first_row, ui->editor_rows, 0, 0);
    if (ui->focus == EDITOR) wmove(ui->input, editor.cursor_row - state->draft.first_row, editor.cursor_column);
}

static bool matches(const char *label, const text_buffer *query)
{
    text_buffer value;
    if (!text_decode(&value, label)) return false;
    for (size_t i = 0; i + query->length <= value.length; ++i) {
        size_t k = 0;
        while (k < query->length && towlower(value.data[i + k]) == towlower(query->data[k])) ++k;
        if (k == query->length) return true;
    }
    return false;
}

static void filter_results(chat_ui *ui)
{
    ui->result_count = 0;
    for (int server = 0; server < SERVERS; ++server) {
        for (int channel = 0; channel < CHANNELS; ++channel) {
            char label[180];
            snprintf(label, sizeof(label), "%s / %s %s", servers[server].name, channel < 6 ? "#" : "@", servers[server].channels[channel]);
            if (matches(label, &ui->query)) ui->results[ui->result_count++] = (nav_result){server, channel};
        }
    }
    if (ui->result_cursor >= ui->result_count) ui->result_cursor = ui->result_count ? ui->result_count - 1 : 0;
    int visible = ui->modal_height - 6;
    if (ui->result_cursor < ui->first_result) ui->first_result = ui->result_cursor;
    if (ui->result_cursor >= ui->first_result + visible) ui->first_result = ui->result_cursor - visible + 1;
    int last = ui->result_count > visible ? ui->result_count - visible : 0;
    if (ui->first_result > last) ui->first_result = last;
}

static const char *help_lines[] = {
    "NAVEGAÇÃO POR PAINEL", "Abas: ← → trocam de servidor", "Abas: ↓ entra nos canais",
    "Canais: ↑ ↓ escolhem canal ou DM", "Canais: → ou Enter abre a conversa", "Canais: ← volta às abas",
    "Canais: ↑ no primeiro volta às abas", "Histórico: ↑ ↓ rolam uma linha", "Histórico: ← volta aos canais",
    "Tab / Shift+Tab: mudar foco", "No estreito: Tab alterna painéis", "Esc: editor → histórico → canais",
    "Mouse: clicar para abrir ou focar", "‹ ›: mostrar mais abas", "Canais / Conversa: painel estreito", "Ctrl+K: procurar servidor ou canal", "",
    "EDITOR", "Digite no histórico: focar editor", "Enter: envio só na demonstração", "Ctrl+N: inserir uma nova linha", "Colagem multilinha preserva linhas", "Colar nunca dispara envio",
    "← →: mover por grafema", "↑ ↓: mover entre linhas", "Home / End: início ou fim da linha", "Backspace / Delete: apagar grafema", "Ctrl+U: limpar todo o rascunho", "",
    "HISTÓRICO", "PgUp / PgDn ou roda: rolar", "Home / End no histórico: início/fim", "F2: chegada de mensagem simulada",
    "Novas mensagens preservam a leitura", "Ctrl+R no erro: tentar novamente", "",
    "DADOS LOCAIS", "Até 2048 caracteres por rascunho", "Até 32 mensagens locais por conversa",
    "Trocar de conversa mantém o estado", "Ctrl+Q: sair; confirmar rascunhos", "Ctrl+C: sair diretamente", "Esc: fechar modal ou confirmação", "Tudo é descartado ao sair", "",
    "UNICODE", "Emojis compostos podem virar ?", "O texto original fica preservado", "A fonte e o terminal definem glifos",
};

static void draw_modal(chat_ui *ui)
{
    if (!ui->modal) return;
    WINDOW *window = ui->modal;
    werase(window);
    wattrset(window, COLOR_PAIR(PANEL_ACCENT));
    box(window, 0, 0);
    const char *titles[] = {"", " Ajuda ", " Navegação rápida ", " Adicionar servidor ", " Sair da demonstração "};
    put_text(window, 0, 2, ui->modal_width - 10, titles[ui->modal_type]);
    put_text(window, 0, ui->modal_width - 7, 5, "[Esc]");
    wattrset(window, COLOR_PAIR(PANEL));
    if (ui->modal_type == NAV_MODAL) {
        filter_results(ui);
        wattrset(window, COLOR_PAIR(PANEL_MUTED));
        put_text(window, 1, 2, ui->modal_width - 4, "Filtrar servidores, canais e DMs");
        wattrset(window, COLOR_PAIR(ui->nav_focus == 0 ? FOCUS_AREA : INPUT));
        mvwhline(window, 2, 2, ' ', ui->modal_width - 4);
        text_layout query;
        text_measure(&ui->query, ui->modal_width - 4, &query);
        text_keep_cursor(&ui->query, &query, 1);
        text_draw(window, &ui->query, &query, ui->query.first_row, 1, 2, 2);
        wattrset(window, COLOR_PAIR(PANEL_MUTED));
        char count[100];
        snprintf(count, sizeof(count), "%zu/128 caracteres · %d resultados", ui->query.length, ui->result_count);
        put_text(window, 3, 2, ui->modal_width - 4, ui->paste_full ? "Colagem limitada a 128 caracteres." : count);
        for (int i = ui->first_result, row = 4; i < ui->result_count && row < ui->modal_height - 2; ++i, ++row) {
            nav_result item = ui->results[i];
            char label[180];
            snprintf(label, sizeof(label), "%c %s / %s %s", i == ui->result_cursor ? '>' : ' ', servers[item.server].name,
                     item.channel < 6 ? "#" : "@", servers[item.server].channels[item.channel]);
            wattrset(window, COLOR_PAIR(i == ui->result_cursor ? (ui->nav_focus ? FOCUSED : PANEL_ACTIVE) : PANEL));
            mvwhline(window, row, 2, ' ', ui->modal_width - 4);
            put_text(window, row, 2, ui->modal_width - 4, label);
        }
        if (!ui->result_count) {
            wattrset(window, COLOR_PAIR(PANEL_MUTED));
            put_text(window, 4, 2, ui->modal_width - 4, "Nenhum resultado.");
        }
        wattrset(window, COLOR_PAIR(PANEL_MUTED));
        put_text(window, ui->modal_height - 2, 2, ui->modal_width - 4, "↑↓ escolher · Enter abrir · Esc voltar");
        if (!ui->nav_focus) wmove(window, 2, 2 + query.cursor_column);
    } else if (ui->modal_type == HELP_MODAL) {
        int count = (int)(sizeof(help_lines) / sizeof(help_lines[0]));
        int visible = ui->modal_height - 4;
        int last = count > visible ? count - visible : 0;
        if (ui->help_first < 0) ui->help_first = 0;
        if (ui->help_first > last) ui->help_first = last;
        for (int i = ui->help_first, row = 1; i < count && row <= visible; ++i, ++row)
            put_text(window, row, 2, ui->modal_width - 4, help_lines[i]);
        wattrset(window, COLOR_PAIR(PANEL_MUTED));
        put_text(window, ui->modal_height - 2, 2, ui->modal_width - 4, "↑↓ / PgUp/PgDn / roda · Esc fechar");
    } else if (ui->modal_type == INFO_MODAL) {
        put_text(window, 2, 2, ui->modal_width - 4, "Esta tela usa servidores simulados.");
        put_text(window, 4, 2, ui->modal_width - 4, "Cadastro de URLs será entregue em R3.");
        put_text(window, 6, 2, ui->modal_width - 4, "Nenhuma credencial é usada aqui.");
        button_draw(window, ui->modal_height - 2, 2, ui->modal_width - 4, "[Voltar]", BUTTON_FOCUSED, BUTTON_PANEL);
    } else {
        put_text(window, 2, 2, ui->modal_width - 4, "Há rascunhos não enviados.");
        put_text(window, 4, 2, ui->modal_width - 4, "Sair descarta todos os dados locais.");
        button_draw(window, ui->modal_height - 2, 2, 6, "[Sair]",
                    ui->modal_choice == 0 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        button_draw(window, ui->modal_height - 2, 12, 8, "[Voltar]",
                    ui->modal_choice == 1 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    }
    wnoutrefresh(window);
}

static void draw(chat_ui *ui)
{
    curs_set(0);
    if (!ui->frame) {
        erase();
        put_text(stdscr, 0, 0, COLS, "Amplie o terminal para 44 colunas e 14 linhas.");
        if (LINES > 1) put_text(stdscr, 1, 0, COLS, "Ctrl+Q: sair");
        refresh();
        return;
    }
    werase(ui->frame);
    wattrset(ui->frame, COLOR_PAIR(BORDER));
    box(ui->frame, 0, 0);
    mvwhline(ui->frame, LINES - 4, 1, ACS_HLINE, COLS - 2);
    if (!ui->narrow) mvwvline(ui->frame, 3, ui->sidebar_width + 1, ACS_VLINE, ui->content_rows);
    draw_tabs(ui);
    draw_channels(ui);
    draw_conversation(ui);
    werase(ui->footer);
    wattrset(ui->footer, COLOR_PAIR(PANEL_MUTED));
    const char *areas[] = {"Abas", "Canais", "Histórico", "Editor"};
    const char *modals[] = {"", "Ajuda", "Navegação rápida", "Adicionar servidor", "Confirmação de saída"};
    char status[160];
    snprintf(status, sizeof(status), "DEMO · visitante · Foco: %s", ui->modal_type == NO_MODAL ? areas[ui->focus] : modals[ui->modal_type]);
    put_text(ui->footer, 0, 1, COLS - 4, ui->notice ? ui->notice : status);
    const char *controls = ui->focus == TABS ? "←→ servidores · ↓ canais · F1: ajuda" :
                           ui->focus == CHANNEL_LIST ? "↑↓ canais · ← abas · → conversa" :
                           ui->focus == HISTORY ? "↑↓ rolar · ← canais · Digite: editar" :
                           "Esc: histórico · Tab: foco · F1: ajuda";
    put_text(ui->footer, 1, 1, COLS - 4, controls);
    touchwin(ui->frame);
    wnoutrefresh(ui->frame);
    wnoutrefresh(ui->tabs);
    if (ui->sidebar) wnoutrefresh(ui->sidebar);
    if (ui->conversation) wnoutrefresh(ui->conversation);
    wnoutrefresh(ui->footer);
    if (ui->input) wnoutrefresh(ui->input);
    draw_modal(ui);
    doupdate();
    bool editor_cursor = ui->modal_type == NO_MODAL && ui->input && ui->focus == EDITOR;
    bool query_cursor = ui->modal && ui->modal_type == NAV_MODAL && !ui->nav_focus;
    curs_set(editor_cursor || query_cursor ? 1 : 0);
}

static bool set_focus(chat_ui *ui, int focus)
{
    ui->focus = focus;
    if (focus == TABS) ui->tab_cursor = ui->server;
    bool channels = focus == CHANNEL_LIST ? true : focus == HISTORY || focus == EDITOR ? false : ui->show_channels;
    if (channels != ui->show_channels) {
        ui->show_channels = channels;
        if (ui->narrow) return layout(ui);
    }
    return true;
}

static bool select_conversation(chat_ui *ui, int server, int channel)
{
    ui->server = ui->tab_cursor = server;
    ui->instances[server].channel = ui->instances[server].channel_cursor = channel;
    conversation *state = current(ui);
    if (state->state == LOADING && !state->loading_until) {
        uint64_t now;
        if (!net_now_ms(&now) || now > UINT64_MAX - 700) state->state = FAILED;
        else state->loading_until = now + 700;
    }
    return true;
}

/* Arrows act on the focused panel. Moving across panels keeps their state;
 * opening a channel is explicit, and history movement uses its own anchor. */
static bool navigate_arrow(chat_ui *ui, wint_t key)
{
    if (ui->focus == TABS) {
        if (key == KEY_DOWN) return set_focus(ui, CHANNEL_LIST);
        if (key == KEY_LEFT || key == KEY_RIGHT) {
            int selected = ui->tab_cursor + (key == KEY_RIGHT ? 1 : -1);
            if (selected < 0 || selected > SERVERS) return true;
            if (selected == SERVERS) { ui->tab_cursor = selected; return true; }
            return select_conversation(ui, selected, ui->instances[selected].channel) && set_focus(ui, TABS);
        }
        return true;
    }
    if (ui->focus == CHANNEL_LIST) {
        instance *state = &ui->instances[ui->server];
        if (key == KEY_LEFT || (key == KEY_UP && state->channel_cursor == 0)) return set_focus(ui, TABS);
        if (key == KEY_RIGHT)
            return select_conversation(ui, ui->server, state->channel_cursor) && set_focus(ui, HISTORY);
        int next = state->channel_cursor + (key == KEY_DOWN ? 1 : -1);
        if (next >= 0 && next < CHANNELS) state->channel_cursor = next;
        return true;
    }
    if (ui->focus == HISTORY) {
        if (key == KEY_LEFT) return set_focus(ui, CHANNEL_LIST);
        if (key == KEY_UP || key == KEY_DOWN) history_move(ui, key == KEY_UP ? -1 : 1);
    }
    return true;
}

static bool has_drafts(chat_ui *ui)
{
    for (int i = 0; i < SERVERS; ++i)
        for (int j = 0; j < CHANNELS; ++j)
            if (ui->instances[i].conversations[j].draft.length) return true;
    return false;
}

static void retry_history(chat_ui *ui)
{
    uint64_t now;
    if (!net_now_ms(&now) || now > UINT64_MAX - 700) {
        current(ui)->state = FAILED;
        ui->notice = "Não foi possível preparar o temporizador da demonstração.";
        return;
    }
    current(ui)->state = LOADING;
    current(ui)->loading_until = now + 700;
    ui->notice = "Carregamento simulado; nenhuma requisição de rede.";
}

static bool append_message(chat_ui *ui, const text_buffer *body, bool mine)
{
    conversation *state = current(ui);
    if (state->state != READY) {
        ui->notice = "Aguarde o carregamento ou tente novamente no erro.";
        return false;
    }
    if (state->message_count == LOCAL_MESSAGES) {
        ui->notice = "Limite de 32 mensagens locais; rascunho preservado.";
        return false;
    }
    char *encoded = malloc(TEXT_UTF8_BYTES);
    if (!encoded || !text_encode(body, encoded)) {
        free(encoded);
        ui->notice = "Não foi possível adicionar a mensagem; rascunho preservado.";
        return false;
    }
    state->messages[state->message_count++] = (local_message){encoded, mine};
    if (!state->at_end) ++state->unseen;
    ui->notice = mine ? "Mensagem adicionada somente à demonstração." : "Mensagem simulada recebida nesta conversa.";
    return true;
}

static void send_draft(chat_ui *ui)
{
    text_buffer *draft = &current(ui)->draft;
    bool content = false;
    for (size_t i = 0; i < draft->length; ++i) if (!iswspace(draft->data[i])) content = true;
    if (!content) { ui->notice = "Escreva uma mensagem antes de enviar."; return; }
    if (append_message(ui, draft, true)) text_clear(draft);
}

static bool activate_selection(chat_ui *ui)
{
    if (ui->focus == TABS) {
        int selected = ui->tab_cursor;
        if (selected == SERVERS) return open_modal(ui, INFO_MODAL);
        return select_conversation(ui, selected, ui->instances[selected].channel) && set_focus(ui, CHANNEL_LIST);
    }
    if (ui->focus == CHANNEL_LIST)
        return select_conversation(ui, ui->server, ui->instances[ui->server].channel_cursor) && set_focus(ui, HISTORY);
    if (ui->focus == EDITOR) send_draft(ui);
    if (ui->focus == HISTORY) {
        if (current(ui)->state == FAILED) retry_history(ui);
        else return set_focus(ui, EDITOR);
    }
    return true;
}

static bool cycle_focus(chat_ui *ui, int direction)
{
    if (!ui->narrow) return set_focus(ui, (ui->focus + direction + FOCUS_COUNT) % FOCUS_COUNT);
    /* Two panels need one keystroke in either direction. The editor is reached
     * by typing, Enter from history, Ctrl+N or a click, and keeps its draft. */
    if (ui->focus == CHANNEL_LIST) return activate_selection(ui);
    if (ui->focus == TABS && ui->show_channels) return set_focus(ui, HISTORY);
    return set_focus(ui, CHANNEL_LIST);
}

static bool in_range(click_range range, int x) { return x >= range.x && x < range.x + range.width; }

static bool navigate_result(chat_ui *ui)
{
    filter_results(ui);
    if (!ui->result_count) return true;
    nav_result item = ui->results[ui->result_cursor];
    close_modal(ui);
    return select_conversation(ui, item.server, item.channel) && set_focus(ui, HISTORY);
}

static bool modal_click(chat_ui *ui, const MEVENT *event, bool *quit)
{
    if (!ui->modal) return true;
    int y = event->y, x = event->x;
    if (!wmouse_trafo(ui->modal, &y, &x, false)) return true;
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        int direction = event->bstate & BUTTON4_PRESSED ? -1 : 1;
        if (ui->modal_type == HELP_MODAL) ui->help_first += direction * 3;
        if (ui->modal_type == NAV_MODAL) {
            int next = ui->result_cursor + direction;
            if (next >= 0 && next < ui->result_count) ui->result_cursor = next;
            ui->nav_focus = 1;
        }
        return true;
    }
    if (y == 0 && x >= ui->modal_width - 7 && x < ui->modal_width - 2) { close_modal(ui); return true; }
    if (ui->modal_type == NAV_MODAL) {
        if (y == 2 && x >= 2 && x < ui->modal_width - 2) {
            ui->nav_focus = 0;
            text_click(&ui->query, ui->modal_width - 4, ui->query.first_row, x - 2);
        } else if (y >= 4 && y < ui->modal_height - 2 && x >= 2 && x < ui->modal_width - 2) {
            int result = ui->first_result + y - 4;
            if (result < ui->result_count) { ui->result_cursor = result; return navigate_result(ui); }
        }
    } else if (y == ui->modal_height - 2) {
        if (ui->modal_type == INFO_MODAL && x >= 2 && x < 10) close_modal(ui);
        if (ui->modal_type == EXIT_MODAL) {
            if (x >= 2 && x < 8) *quit = true;
            if (x >= 12 && x < 20) close_modal(ui);
        }
    }
    return true;
}

static bool click(chat_ui *ui, bool *quit)
{
    MEVENT event;
    if (ui_mouse_get(&event) != OK || ui->paste || !ui->frame ||
        !(event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED))) return true;
    if (ui->modal_type != NO_MODAL) return modal_click(ui, &event, quit);
    int y = event.y, x = event.x;
    if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        if (ui->conversation && wmouse_trafo(ui->conversation, &y, &x, false))
            history_move(ui, event.bstate & BUTTON4_PRESSED ? -3 : 3);
        return true;
    }
    ui->notice = NULL;
    if (wmouse_trafo(ui->tabs, &y, &x, false)) {
        if (y == 0) {
            int direction = in_range(ui->tabs_back, x) ? -1 : in_range(ui->tabs_next, x) ? 1 : 0;
            if (direction) {
                int selected = ui->first_tab - 1;
                if (direction > 0) for (int i = 0; i <= SERVERS; ++i) if (ui->tab_hits[i].width) selected = i + 1;
                if (selected < 0 || selected > SERVERS) return true;
                if (!set_focus(ui, TABS)) return false;
                ui->tab_cursor = selected;
                return true;
            }
            for (int i = 0; i <= SERVERS; ++i) {
                if (!in_range(ui->tab_hits[i], x)) continue;
                if (!set_focus(ui, TABS)) return false;
                ui->tab_cursor = i;
                return activate_selection(ui);
            }
        } else if (y == 1 && ui->narrow) {
            int focus = in_range(ui->channels_link, x) ? CHANNEL_LIST : in_range(ui->conversation_link, x) ? HISTORY : -1;
            if (focus >= 0) return set_focus(ui, focus);
        }
        return true;
    }
    y = event.y; x = event.x;
    if (ui->sidebar && wmouse_trafo(ui->sidebar, &y, &x, false)) {
        if (y < 1 || y > SIDEBAR_ROWS || x < 1 || x >= ui->sidebar_width - 1) return true;
        int selected = ui->channel_hits[y - 1];
        if (selected < 0) return true;
        if (!set_focus(ui, CHANNEL_LIST)) return false;
        ui->instances[ui->server].channel_cursor = selected;
        return activate_selection(ui);
    }
    y = event.y; x = event.x;
    if (ui->input && wmouse_trafo(ui->input, &y, &x, false)) {
        if (!set_focus(ui, EDITOR)) return false;
        text_buffer *draft = &current(ui)->draft;
        text_click(draft, ui->content_width - 6, draft->first_row + y, x);
        return true;
    }
    y = event.y; x = event.x;
    if (ui->conversation && wmouse_trafo(ui->conversation, &y, &x, false)) {
        if (x < 1 || x >= ui->content_width - 1) return true;
        if (y == 0 && in_range(ui->history_end, x)) { current(ui)->at_end = true; current(ui)->unseen = 0; }
        else if (y == 3 && in_range(ui->retry, x)) retry_history(ui);
        else if (y == ui->editor_border && in_range(ui->editor_label, x)) {
            if (!set_focus(ui, EDITOR)) return false;
            current(ui)->draft.cursor = current(ui)->draft.length;
        } else if (y < ui->editor_border && y != 2) return set_focus(ui, HISTORY);
    }
    return true;
}

static bool enter_key(int type, wint_t key)
{
    return (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
}

static void edit_key(text_buffer *buffer, int width, int type, wint_t key)
{
    if (type == KEY_CODE_YES) {
        switch (key) {
            case KEY_LEFT: text_horizontal(buffer, false); break;
            case KEY_RIGHT: text_horizontal(buffer, true); break;
            case KEY_UP: text_vertical(buffer, width, -1); break;
            case KEY_DOWN: text_vertical(buffer, width, 1); break;
            case KEY_HOME: text_line_edge(buffer, width, false); break;
            case KEY_END: text_line_edge(buffer, width, true); break;
            case KEY_BACKSPACE: text_delete(buffer, true); break;
            case KEY_DC: text_delete(buffer, false); break;
            default: break;
        }
    } else if (key == 127 || key == 8) text_delete(buffer, true);
    else if (key == 21) text_clear(buffer);
}

static bool modal_key(chat_ui *ui, int type, wint_t key, bool *quit)
{
    if (type == OK && key == 27) { close_modal(ui); return true; }
    if (ui->modal_type == EXIT_MODAL) {
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && (key == KEY_BTAB || key == KEY_LEFT || key == KEY_RIGHT)))
            ui->modal_choice = !ui->modal_choice;
        if (enter_key(type, key)) { if (!ui->modal_choice) *quit = true; else close_modal(ui); }
    } else if (ui->modal_type == INFO_MODAL) {
        if (enter_key(type, key)) close_modal(ui);
    } else if (ui->modal_type == HELP_MODAL) {
        if (type == KEY_CODE_YES) {
            if (key == KEY_UP) --ui->help_first;
            if (key == KEY_DOWN) ++ui->help_first;
            if (key == KEY_PPAGE) ui->help_first -= ui->modal_height - 4;
            if (key == KEY_NPAGE) ui->help_first += ui->modal_height - 4;
        }
    } else if (ui->modal_type == NAV_MODAL) {
        if (enter_key(type, key)) return navigate_result(ui);
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) ui->nav_focus = !ui->nav_focus;
        else if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE)) {
            int delta = key == KEY_UP ? -1 : key == KEY_DOWN ? 1 : key == KEY_PPAGE ? -(ui->modal_height - 6) : ui->modal_height - 6;
            ui->result_cursor += delta;
            if (ui->result_cursor < 0) ui->result_cursor = 0;
            filter_results(ui);
            ui->nav_focus = 1;
        } else {
            ui->nav_focus = 0;
            ui->paste_full = false;
            edit_key(&ui->query, ui->modal_width - 4, type, key);
            if (type == OK && iswprint(key) && ui->query.length < 128) text_insert(&ui->query, key, false);
            ui->result_cursor = ui->first_result = 0;
        }
    }
    return true;
}

static void paste_character(chat_ui *ui, wint_t key)
{
    text_buffer *target = ui->modal_type == NAV_MODAL ? &ui->query :
                          ui->modal_type == NO_MODAL && ui->focus == EDITOR && ui->input ? &current(ui)->draft : NULL;
    if (!target) return;
    if (key == '\n' && ui->paste_cr) { ui->paste_cr = false; return; }
    ui->paste_cr = key == '\r';
    if (ui->modal_type == NAV_MODAL && (key == '\r' || key == '\n')) return;
    if (ui->modal_type == NAV_MODAL && target->length >= 128) { ui->paste_full = true; return; }
    if (!text_insert(target, key, ui->modal_type == NO_MODAL) && target->length >= TEXT_LIMIT) ui->paste_full = true;
    if (ui->modal_type == NAV_MODAL) ui->result_cursor = ui->first_result = 0;
}

int chat_demo_run(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("Abra o papo-tui --demo em um terminal interativo.\n", stderr);
        return 1;
    }
    chat_ui *ui = calloc(1, sizeof(*ui));
    if (!ui) { fputs("Não foi possível preparar a demonstração.\n", stderr); return 1; }
    ui->show_channels = true;
    for (int i = 0; i < SERVERS; ++i) {
        for (int j = 0; j < CHANNELS; ++j) {
            conversation *state = &ui->instances[i].conversations[j];
            state->at_end = true;
            state->state = j == 3 ? LOADING : j == 4 ? FAILED : READY;
            text_clear(&state->draft);
        }
    }
    text_clear(&ui->query);
    init_graphemebreak();
    if (!initscr()) { free(ui); return 1; }
    raw(); nonl(); noecho(); keypad(stdscr, true);
    mouseinterval(0);
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
    timeout(30); set_escdelay(25);
    ui_theme theme;
    theme_init(&theme);
    struct sigaction action = {0}, previous_int, previous_term;
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &previous_int);
    sigaction(SIGTERM, &action, &previous_term);
    define_key("\033[200~", KEY_PASTE_START);
    define_key("\033[201~", KEY_PASTE_END);
    fputs("\033[?2004h", stdout); fflush(stdout);
    int result = layout(ui) ? 0 : 1;
    bool redraw = true, quit = false;
    unsigned paste_steps = 0;
    while (!result && !quit && !interrupted) {
        conversation *state = current(ui);
        if (state->state == LOADING && state->loading_until) {
            uint64_t now;
            if (!net_now_ms(&now)) { state->state = FAILED; redraw = true; }
            else if (now >= state->loading_until) {
                state->state = READY;
                state->loading_until = 0;
                redraw = true;
            }
        }
        if (redraw && (!ui->paste || paste_steps >= 32)) {
            draw(ui); redraw = false; paste_steps = 0;
        }
        wint_t key;
        int type = graphics_terminal_input(&key);
        if (type == ERR) {
            if (redraw) { draw(ui); redraw = false; paste_steps = 0; }
            continue;
        }
        redraw = true;
        if (type == KEY_CODE_YES && key == KEY_RESIZE) {
            if (!layout(ui)) result = 1;
            paste_steps = 32;
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_PASTE_START) {
            if (ui->frame && ui->modal_type == NO_MODAL && ui->focus == HISTORY && !set_focus(ui, EDITOR)) {
                result = 1;
                continue;
            }
            ui->paste = true; ui->paste_cr = ui->paste_full = false; paste_steps = 32; continue;
        }
        if (type == KEY_CODE_YES && key == KEY_PASTE_END) {
            ui->paste = false;
            if (ui->paste_full) ui->notice = "Colagem limitada pelo tamanho do campo; nenhum envio realizado.";
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_MOUSE) { if (!click(ui, &quit)) result = 1; continue; }
        if (ui->paste) {
            if (type == OK) paste_character(ui, key);
            ++paste_steps;
            continue;
        }
        if (type == OK && (key == 17 || key == 3)) {
            if (key == 3 || ui->modal_type == EXIT_MODAL || !has_drafts(ui)) quit = true;
            else if (!open_modal(ui, EXIT_MODAL)) result = 1;
            continue;
        }
        if (!ui->frame) continue;
        ui->notice = NULL;
        if (type == KEY_CODE_YES && key == KEY_F(1) && ui->modal_type != EXIT_MODAL) {
            if (ui->modal_type == HELP_MODAL) close_modal(ui);
            else if (!open_modal(ui, HELP_MODAL)) result = 1;
            continue;
        }
        if (type == OK && key == 11 && ui->modal_type != EXIT_MODAL) {
            if (!open_modal(ui, NAV_MODAL)) result = 1;
            continue;
        }
        if (ui->modal_type != NO_MODAL) { if (!modal_key(ui, type, key, &quit)) result = 1; continue; }
        if (type == OK && key == 27) {
            int focus = ui->focus == EDITOR ? HISTORY : ui->focus == HISTORY ? CHANNEL_LIST :
                        ui->focus == CHANNEL_LIST ? HISTORY : ui->show_channels ? CHANNEL_LIST : HISTORY;
            ui->tab_cursor = ui->server;
            if (!set_focus(ui, focus)) result = 1;
            continue;
        }
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
            if (!cycle_focus(ui, type == OK ? 1 : -1)) result = 1;
            continue;
        }
        if (type == KEY_CODE_YES && ui->focus != EDITOR &&
            (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN)) {
            if (!navigate_arrow(ui, key)) result = 1;
            continue;
        }
        if (enter_key(type, key)) { if (!activate_selection(ui)) result = 1; continue; }
        if (type == KEY_CODE_YES && ui->conversation && (key == KEY_PPAGE || key == KEY_NPAGE)) {
            history_move(ui, key == KEY_PPAGE ? -ui->history_height : ui->history_height);
            continue;
        }
        if (type == KEY_CODE_YES && ui->focus == HISTORY && (key == KEY_HOME || key == KEY_END)) {
            if (key == KEY_HOME) history_move(ui, -INT_MAX / 2);
            else { current(ui)->at_end = true; current(ui)->unseen = 0; }
            continue;
        }
        if (type == OK && ui->focus == HISTORY && key == 18 && current(ui)->state == FAILED) {
            retry_history(ui); continue;
        }
        if (type == KEY_CODE_YES && key == KEY_F(2)) {
            text_buffer body;
            text_decode(&body, "Esta mensagem acabou de chegar na demonstração. Seu ponto de leitura permanece onde estava.");
            append_message(ui, &body, false);
            continue;
        }
        /* Opening a conversation keeps history focused for reading. Text
         * moves into its draft without consuming the first character or
         * changing the saved cursor and reading anchor. */
        if (ui->focus == HISTORY && type == OK && (iswprint(key) || key == 14 || key == 0x200c || key == 0x200d)) {
            if (!set_focus(ui, EDITOR)) { result = 1; continue; }
        }
        if (ui->focus == EDITOR && ui->input) {
            text_buffer *draft = &current(ui)->draft;
            edit_key(draft, ui->content_width - 6, type, key);
            if (type == OK && (key == 14 || iswprint(key) || key == 0x200c || key == 0x200d)) {
                if (!text_insert(draft, key == 14 ? '\n' : key, true)) ui->notice = "Limite do rascunho: 2048 caracteres Unicode.";
            }
        }
    }
    destroy_layout(ui);
    fputs("\033[?2004l", stdout); fflush(stdout);
    mousemask(0, NULL);
    theme_restore(&theme);
    endwin();
    sigaction(SIGINT, &previous_int, NULL);
    sigaction(SIGTERM, &previous_term, NULL);
    for (int i = 0; i < SERVERS; ++i)
        for (int j = 0; j < CHANNELS; ++j)
            for (int k = 0; k < ui->instances[i].conversations[j].message_count; ++k)
                free(ui->instances[i].conversations[j].messages[k].body);
    free(ui);
    if (result) fputs("Não foi possível criar a demonstração.\n", stderr);
    return result;
}
