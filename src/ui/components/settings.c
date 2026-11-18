#include "ui/components/settings.h"
#include "app/update.h"
#include "model/conversation/profile_edit.h"
#include "model/text/translation.h"
#include "platform/clipboard.h"
#include "ui/components/button.h"
#include "ui/components/input_border.h"
#include "ui/components/tabs.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include "ui/input/shortcuts.h"
#include "ui/input/text.h"
#include <string.h>
/* Without graphics Normal is not offered, and a saved Normal shows as Light. */
static int first_mode(app_settings *s)
{
    if (app_mode_normal_available())
        return APP_MODE_NORMAL;
    if (s->draft == APP_MODE_NORMAL)
        s->draft = APP_MODE_LIGHT;
    if (s->selection < APP_MODE_LIGHT)
        s->selection = APP_MODE_LIGHT;
    return APP_MODE_LIGHT;
}
static const char *sections[SETTINGS_SECTIONS] = {
    N_("Perfil"),  N_("Aparência"), N_("Notificações"), N_("Áudio"), N_("Acessibilidade"),
    N_("Atalhos"), N_("Sistema"),   N_("Conta"),        N_("Sobre")};
/* Sections that have controls between the tabs and the buttons. */
static bool has_content(int section)
{
    return section == SETTINGS_APPEARANCE || section == SETTINGS_PROFILE ||
           section == SETTINGS_NOTIFICATIONS || section == SETTINGS_AUDIO ||
           section == SETTINGS_SHORTCUTS || section == SETTINGS_ACCESSIBILITY ||
           section == SETTINGS_ACCOUNT || section == SETTINGS_ABOUT;
}
/* Password text never outlives the window. */
static void account_erase(app_settings *s)
{
    for (int i = 0; i < 2; ++i) {
        volatile unsigned char *p = (volatile unsigned char *)&s->account[i];
        for (size_t n = 0; n < sizeof(s->account[i]); ++n)
            p[n] = 0;
        text_clear(&s->account[i]);
        s->account_first[i] = 0;
    }
    s->account_bad = 0;
}
void settings_close(app_settings *s)
{
    if (s->ptt_capture || s->keys_capture) {
        text_capture_cancel();
        s->ptt_capture = s->keys_capture = false;
    }
    if (s->open) {
        voice_test(s->voice, false);
        if (!s->applied)
            voice_configure(&s->original_audio);
        /* A change that has not left yet is dropped with the window. */
        if (s->profile)
            history_password_cancel(s->profile);
    }
    account_erase(s);
    s->account_confirm = false;
    s->open = false;
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
}
/* Sistema shows a copy: the configuration is only at hand while a key is handled. */
static void file_snapshot(app_settings *s, const profile_config *config)
{
    profiles_path(config, s->config_path, sizeof(s->config_path));
    s->file_state = config->file_state;
    s->file_version = config->file_version;
    s->file_writable = config->writable;
    s->file_existed = config->existed;
}
void settings_open(app_settings *s, const profile_config *config, voice_client *voice,
                   history *profile, auth_client *auth, icon_view *icons, const char *server)
{
    settings_close(s);
    memset(s, 0, sizeof(*s));
    s->profile = profile;
    s->auth = auth;
    s->icons = icons;
    snprintf(s->server, sizeof(s->server), "%s", server ? server : "");
    s->open = true;
    s->draft = config->mode;
    s->language = config->language;
    s->update_check = config->update_check;
    file_snapshot(s, config);
    s->selection = config->mode;
    s->audio = s->original_audio = config->audio;
    s->voice = voice;
    s->access = config->accessibility;
    keymap_table_from_overrides(&s->keys, &config->shortcuts);
    s->keys_action = s->keys_conflict = KEYMAP_NONE;
    s->user = s->user_base = *history_user_config(profile);
    s->user_revision = history_user_settings_revision(profile);
    tab_strip_reset(&s->tabs);
    voice_refresh_devices(voice);
    voice_ptt(false);
}
static const char *device_name(app_settings *s, const char *id, bool input)
{
    if (!*id)
        return _("Padrão do sistema");
    size_t i;
    json_t *device;
    json_array_foreach(voice_devices(s->voice), i, device)
    {
        const char *key = json_string_value(json_object_get(device, "id"));
        if (key && !strcmp(id, key) && json_is_true(json_object_get(device, "input")) == input)
            return json_string_value(json_object_get(device, "name"));
    }
    return _("Dispositivo indisponível");
}
static void begin_capture(app_settings *s)
{
    if (!voice_available() || s->ptt_capture)
        return;
    voice_test(s->voice, false);
    if (!text_capture_begin()) {
        snprintf(s->error, sizeof(s->error), _("Não foi possível capturar a tecla."));
        return;
    }
    s->ptt_capture = true;
    s->error[0] = 0;
}
static void audio_draw(app_settings *s, WINDOW *w, int height, int width)
{
    bool voice = voice_available();
    int rows = height - 11;
    if (rows < 1)
        rows = 1;
    if (s->audio_selection < s->audio_first)
        s->audio_first = s->audio_selection;
    if (s->audio_selection >= s->audio_first + rows)
        s->audio_first = s->audio_selection - rows + 1;
    for (int row = 0; row < rows && row + s->audio_first < 9; ++row) {
        int item = row + s->audio_first;
        char label[1024];
        switch (item) {
        case 0:
            snprintf(label, sizeof(label), _("Entrada: < %s >"),
                     voice ? device_name(s, s->audio.input, true) : _("Indisponível"));
            break;
        case 1:
            snprintf(label, sizeof(label), _("Saída: < %s >"),
                     voice ? device_name(s, s->audio.output, false) : _("Indisponível"));
            break;
        case 2:
            snprintf(label, sizeof(label), _("Volume do microfone: < %d%% >"),
                     s->audio.microphone_volume);
            break;
        case 3:
            snprintf(label, sizeof(label), _("Volume da chamada: < %d%% >"),
                     s->audio.output_volume);
            break;
        case 4:
            snprintf(label, sizeof(label), _("[%s teste do microfone]"),
                     voice_testing() ? _("Parar") : _("Iniciar"));
            break;
        case 5:
            snprintf(label, sizeof(label), _("Pressionar para falar: < %s >"),
                     s->audio.push_to_talk ? _("Ativado") : _("Desativado"));
            break;
        case 6: {
            char key[64] = "?";
            if (!s->ptt_capture)
                key_binding_label(&s->audio.ptt, key, sizeof(key));
            snprintf(label, sizeof(label), _("Tecla para falar: < %s >"),
                     s->ptt_capture ? _("pressione uma combinação") : key);
            break;
        }
        case 7:
            snprintf(label, sizeof(label), _("Fluxos desta instância: < %u >"),
                     audio_config_slots(&s->audio, voice_instance(s->voice)));
            break;
        default:
            snprintf(label, sizeof(label), _("[Atualizar dispositivos]"));
            break;
        }
        bool focused = s->focus == 1 && s->audio_selection == item;
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                        (focused ? theme_focus_attributes() : 0));
        text_label(w, row + 5, 2, width - 4, label);
    }
    double rms = voice_testing() ? voice_level(s->voice, false) : -120;
    double peak = voice_testing() ? voice_level(s->voice, true) : -120;
    int length = width - 18;
    int filled = rms <= -60 ? 0 : rms >= 0 ? length : (int)((rms + 60) * length / 60);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 6, 2, 7, _("Nível:"));
    for (int i = 0; i < length; ++i) {
        wattrset(w,
                 COLOR_PAIR(i < filled ? theme_volume_pair(i * 100 / length) : THEME_PANEL_MUTED));
        text_label(w, height - 6, 9 + i, 1, i < filled ? "█" : "░");
    }
    char number[32];
    snprintf(number, sizeof(number), "%.0f dB", peak < -99 ? -99.0 : peak);
    wattrset(w, COLOR_PAIR(peak >= -1 ? THEME_ERROR : THEME_PANEL));
    text_label(w, height - 6, width - 9, 7, number);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(
        w, height - 5, 2, width - 4,
        !voice ? _("Voz indisponível: esta compilação não inclui suporte WebRTC.")
        : s->ptt_capture
            ? _("Pressione a combinação completa; Ctrl/Alt sozinhos não contam. Esc cancela. "
                "Mouse 4/5.")
        : voice_testing() ? _("Retorno local ativo; sua voz não é enviada à chamada.")
        : s->focus == 1 && s->audio_selection == 6
            ? _("Enter captura qualquer combinação ou os botões 4/5 do mouse.")
            : _("←/→ ajusta; Enter alterna. O teste reproduz sua voz localmente."));
    text_label(w, height - 4, 2, width - 4,
               !voice ? _("As opções de áudio ficam disponíveis quando o WebRTC estiver presente.")
               : s->audio_selection == 7
                   ? _("Use o valor VOICE_AUDIO_SLOTS do servidor; vale no próximo ingresso.")
               : s->audio.push_to_talk && s->audio.ptt.kind != KEY_BINDING_MOUSE &&
                       !text_keyboard_events_supported()
                   ? key_binding_legacy_ctrl_byte(&s->audio.ptt)
                         ? _("Terminal sem soltura: a tecla alterna o microfone.")
                         : _("Tecla sem eventos neste terminal: use o botão Segure para falar.")
                   : _("O microfone e a saída podem ser silenciados ao lado de ⚙."));
}
/* Atalhos: every command grouped by area, push-to-talk and the fixed keys,
 * filtered by the search; Enter on a command opens its menu. */
typedef enum { KEYS_HEADER, KEYS_ACTION, KEYS_PTT, KEYS_FIXED, KEYS_RESET } keys_row_kind;
typedef struct {
    keys_row_kind kind;
    int index; /* Action, fixed key, or the header's first action. */
    const char *title;
} keys_row;
#define KEYS_ROWS 128
enum { KEYS_MENU_SET, KEYS_MENU_ADD, KEYS_MENU_REMOVE, KEYS_MENU_DEFAULT, KEYS_MENU_ITEMS };
static const char *const keys_menu_labels[KEYS_MENU_ITEMS] = {
    N_("Definir tecla"), N_("Adicionar outra tecla"), N_("Remover as teclas"),
    N_("Restaurar o padrão")};

static void keys_text(const app_settings *s, keymap_action action, char *output, size_t size)
{
    output[0] = 0;
    size_t used = 0;
    if (!s->keys.count[action]) {
        snprintf(output, size, "%s", _("sem tecla"));
        return;
    }
    for (unsigned i = 0; i < s->keys.count[action]; ++i) {
        char label[64];
        if (!key_binding_label(&s->keys.keys[action][i], label, sizeof(label)))
            continue;
        int written = snprintf(output + used, size - used, "%s%s", used ? " / " : "", label);
        if (written < 0 || (size_t)written >= size - used)
            return;
        used += (size_t)written;
    }
}

static bool keys_match(const char *needle, const char *a, const char *b, const char *c)
{
    if (!needle || !*needle)
        return true;
    char text[1024];
    snprintf(text, sizeof(text), "%s %s %s", a ? a : "", b ? b : "", c ? c : "");
    char *fold = g_utf8_casefold(text, -1);
    bool found = fold && strstr(fold, needle);
    g_free(fold);
    return found;
}

/* Rows matching the search, with a header above each group that has any. */
static int keys_rows(const app_settings *s, keys_row *rows)
{
    char query[TEXT_UTF8_BYTES];
    if (!text_encode(&s->keys_search, query))
        query[0] = 0;
    char *needle = g_utf8_casefold(query, -1);
    int count = 0;
    const char *group = NULL;
    for (int i = 0; i < KEYMAP_ACTIONS; ++i) {
        keymap_action action = (keymap_action)i;
        char keys[256];
        keys_text(s, action, keys, sizeof(keys));
        const char *area = keymap_action_group(action);
        if (!keys_match(needle, keymap_action_label(action), keys, area))
            continue;
        if (area != group) {
            rows[count++] = (keys_row){KEYS_HEADER, i, area};
            group = area;
        }
        rows[count++] = (keys_row){KEYS_ACTION, i, NULL};
    }
    if (voice_available()) {
        char key[64] = "";
        key_binding_label(&s->audio.ptt, key, sizeof(key));
        if (keys_match(needle, _("Pressionar para falar"), key, _("Áudio"))) {
            rows[count++] = (keys_row){KEYS_HEADER, -1, _("Áudio")};
            rows[count++] = (keys_row){KEYS_PTT, -1, NULL};
        }
    }
    bool fixed_header = false;
    for (int i = 0; i < keymap_fixed_count(); ++i) {
        const char *keys, *function;
        keymap_fixed(i, &keys, &function);
        if (!keys_match(needle, function, keys, _("Teclas fixas")))
            continue;
        if (!fixed_header) {
            rows[count++] = (keys_row){KEYS_HEADER, -1, _("Teclas fixas")};
            fixed_header = true;
        }
        rows[count++] = (keys_row){KEYS_FIXED, i, NULL};
    }
    if (!needle || !*needle)
        rows[count++] = (keys_row){KEYS_RESET, -1, NULL};
    g_free(needle);
    return count;
}

static bool keys_selectable(const keys_row *row)
{
    return row->kind != KEYS_HEADER;
}

/* Keeps the selection on a row that can be chosen and inside the list. */
static void keys_clamp(app_settings *s, const keys_row *rows, int count)
{
    if (s->keys_selection >= count)
        s->keys_selection = count - 1;
    if (s->keys_selection < 0)
        s->keys_selection = 0;
    while (s->keys_selection < count - 1 && !keys_selectable(&rows[s->keys_selection]))
        ++s->keys_selection;
}

static void keys_move(app_settings *s, int step)
{
    keys_row rows[KEYS_ROWS];
    int count = keys_rows(s, rows);
    if (!count)
        return;
    int at = s->keys_selection + step;
    while (at >= 0 && at < count && !keys_selectable(&rows[at]))
        at += step > 0 ? 1 : -1;
    if (at < 0 || at >= count) {
        if (step < 0)
            s->keys_list = false;
        return;
    }
    s->keys_selection = at;
}

static void keys_capture_begin(app_settings *s, bool add)
{
    if (!text_capture_begin()) {
        snprintf(s->error, sizeof(s->error), _("Não foi possível capturar a tecla."));
        return;
    }
    s->keys_capture = true;
    s->keys_add = add;
    s->error[0] = 0;
}

static void keys_store(app_settings *s, keymap_action action, const key_binding *key)
{
    unsigned *count = &s->keys.count[action];
    if (!s->keys_add)
        *count = 0;
    for (unsigned i = 0; i < *count; ++i)
        if (key_binding_equal(&s->keys.keys[action][i], key))
            return;
    if (*count < KEYMAP_BINDINGS)
        s->keys.keys[action][(*count)++] = *key;
}

/* Takes a key for the selected command: refused keys explain why, a key of
 * another command waits for confirmation. */
static void keys_captured(app_settings *s, key_binding key)
{
    keymap_action action = s->keys_action;
    keymap_binding_normalize(&key);
    const char *reason = keymap_binding_error(action, &key);
    if (reason) {
        keys_capture_begin(s, s->keys_add);
        snprintf(s->error, sizeof(s->error), "%s", reason);
        return;
    }
    keymap_conflict conflict = keymap_find_conflict(&s->keys, action, &key);
    char label[64] = "";
    key_binding_label(&key, label, sizeof(label));
    if (conflict.kind == KEYMAP_CONFLICT_FIXED) {
        keys_capture_begin(s, s->keys_add);
        snprintf(s->error, sizeof(s->error), _("%s é fixa nesta área: %s."), label, conflict.label);
        return;
    }
    if (conflict.kind == KEYMAP_CONFLICT_ACTION) {
        s->keys_pending = key;
        s->keys_conflict = conflict.action;
        return;
    }
    keys_store(s, action, &key);
    if (voice_available() && s->audio.push_to_talk && key_binding_equal(&s->audio.ptt, &key) &&
        keymap_conversation_action(action))
        snprintf(s->error, sizeof(s->error),
                 _("Durante chamadas, %s transmite a voz em vez de \"%s\"."), label,
                 keymap_action_label(action));
}

/* Moves the pending key from the command that had it to the selected one. */
static void keys_replace(app_settings *s)
{
    keymap_action other = s->keys_conflict;
    s->keys_conflict = KEYMAP_NONE;
    unsigned kept = 0;
    for (unsigned i = 0; i < s->keys.count[other]; ++i)
        if (!key_binding_equal(&s->keys.keys[other][i], &s->keys_pending))
            s->keys.keys[other][kept++] = s->keys.keys[other][i];
    if (!kept && (keymap_action_flags(other) & KEYMAP_REQUIRED)) {
        snprintf(s->error, sizeof(s->error), _("%s precisa de pelo menos uma tecla."),
                 keymap_action_label(other));
        return;
    }
    s->keys.count[other] = kept;
    keys_store(s, s->keys_action, &s->keys_pending);
}

static void keys_menu_activate(app_settings *s)
{
    keymap_action action = s->keys_action;
    s->keys_menu = false;
    switch (s->keys_menu_item) {
    case KEYS_MENU_SET:
        keys_capture_begin(s, false);
        break;
    case KEYS_MENU_ADD:
        if (s->keys.count[action] >= KEYMAP_BINDINGS)
            snprintf(s->error, sizeof(s->error), _("Esta função já tem %d teclas."),
                     KEYMAP_BINDINGS);
        else
            keys_capture_begin(s, true);
        break;
    case KEYS_MENU_REMOVE:
        if (keymap_action_flags(action) & KEYMAP_REQUIRED)
            snprintf(s->error, sizeof(s->error), _("%s precisa de pelo menos uma tecla."),
                     keymap_action_label(action));
        else
            s->keys.count[action] = 0;
        break;
    default: {
        keymap_table defaults;
        keymap_table_defaults(&defaults);
        s->keys.count[action] = defaults.count[action];
        memcpy(s->keys.keys[action], defaults.keys[action], sizeof(defaults.keys[action]));
        break;
    }
    }
}

static void keys_activate(app_settings *s)
{
    keys_row rows[KEYS_ROWS];
    int count = keys_rows(s, rows);
    if (s->keys_selection < 0 || s->keys_selection >= count)
        return;
    const keys_row *row = &rows[s->keys_selection];
    s->error[0] = 0;
    if (row->kind == KEYS_ACTION) {
        s->keys_action = (keymap_action)row->index;
        s->keys_menu = true;
        s->keys_menu_item = KEYS_MENU_SET;
    } else if (row->kind == KEYS_PTT)
        begin_capture(s);
    else if (row->kind == KEYS_FIXED)
        snprintf(s->error, sizeof(s->error), _("Teclas fixas não podem ser alteradas."));
    else if (row->kind == KEYS_RESET)
        s->keys_reset_confirm = true;
}

static int keys_list_rows(int height)
{
    int rows = height - 9;
    return rows < 1 ? 1 : rows;
}

static void keys_menu_draw(app_settings *s, WINDOW *w, int width, int row_y)
{
    int menu_width = 30, menu_height = KEYS_MENU_ITEMS + 2;
    int x = width - menu_width - 3, y = row_y + 1;
    if (y + menu_height > getmaxy(w) - 3)
        y = row_y - menu_height;
    if (y < 5)
        y = 5;
    s->keys_menu_y = y;
    s->keys_menu_x = x;
    WINDOW *menu = derwin(w, menu_height, menu_width, y, x);
    if (!menu)
        return;
    wbkgd(menu, COLOR_PAIR(THEME_PANEL));
    werase(menu);
    wattrset(menu, COLOR_PAIR(THEME_BORDER));
    box(menu, 0, 0);
    for (int i = 0; i < KEYS_MENU_ITEMS; ++i) {
        bool focused = s->keys_menu_item == i;
        wattrset(menu, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                           (focused ? theme_focus_attributes() : 0));
        mvwhline(menu, i + 1, 1, ' ', menu_width - 2);
        text_label(menu, i + 1, 2, menu_width - 4, _(keys_menu_labels[i]));
    }
    delwin(menu);
}

static void keys_draw(app_settings *s, WINDOW *w, int height, int width)
{
    int pair = s->focus == 1 && !s->keys_list ? THEME_FOCUSED : THEME_INPUT;
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    text_label(w, 5, 2, 8, _("Buscar:"));
    wattrset(w, COLOR_PAIR(pair));
    mvwhline(w, 5, 10, ' ', width - 12);
    text_draw_field(w, &s->keys_search, &s->keys_search_first, 5, 10, width - 12, false);
    int cursor = getcurx(w);
    if (!s->keys_search.length) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 5, 10, width - 12, _("função, tecla ou área"));
    }
    if (s->focus == 1 && !s->keys_list && !s->keys_menu && !s->keys_capture)
        mvwchgat(w, 5, cursor, 1, A_REVERSE, pair, NULL);
    keys_row rows[KEYS_ROWS];
    int count = keys_rows(s, rows), visible = keys_list_rows(height);
    keys_clamp(s, rows, count);
    if (s->keys_selection < s->keys_first)
        s->keys_first = s->keys_selection;
    /* Keep the group title visible above the first command of a group. */
    if (s->keys_first == s->keys_selection && s->keys_first > 0 &&
        rows[s->keys_first - 1].kind == KEYS_HEADER)
        --s->keys_first;
    if (s->keys_selection >= s->keys_first + visible)
        s->keys_first = s->keys_selection - visible + 1;
    if (s->keys_first > count - visible)
        s->keys_first = count - visible > 0 ? count - visible : 0;
    int keys_width = width >= 60 ? 26 : 18, menu_row = 6;
    if (!count) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 6, 2, width - 4, _("Nenhum atalho encontrado."));
    }
    for (int line = 0; line < visible && s->keys_first + line < count; ++line) {
        int index = s->keys_first + line, y = 6 + line;
        const keys_row *row = &rows[index];
        bool focused = s->focus == 1 && s->keys_list && index == s->keys_selection;
        if (focused)
            menu_row = y;
        if (row->kind == KEYS_HEADER) {
            wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
            text_label(w, y, 2, width - 4, row->title);
            continue;
        }
        int base = focused ? THEME_FOCUSED : THEME_PANEL;
        wattrset(w, COLOR_PAIR(base) | (focused ? theme_focus_attributes() : 0));
        mvwhline(w, y, 2, ' ', width - 4);
        char keys[256] = "", label[512] = "";
        bool fixed = row->kind == KEYS_FIXED, changed = false;
        if (row->kind == KEYS_ACTION) {
            keymap_action action = (keymap_action)row->index;
            keys_text(s, action, keys, sizeof(keys));
            keymap_table defaults;
            keymap_table_defaults(&defaults);
            changed = defaults.count[action] != s->keys.count[action] ||
                      memcmp(defaults.keys[action], s->keys.keys[action],
                             sizeof(key_binding) * s->keys.count[action]);
            snprintf(label, sizeof(label), "%s", keymap_action_label(action));
        } else if (row->kind == KEYS_PTT) {
            if (s->ptt_capture)
                snprintf(keys, sizeof(keys), "%s", _("pressione uma combinação"));
            else
                key_binding_label(&s->audio.ptt, keys, sizeof(keys));
            snprintf(label, sizeof(label), "%s", _("Pressionar para falar"));
        } else if (fixed) {
            const char *fixed_keys, *function;
            keymap_fixed(row->index, &fixed_keys, &function);
            snprintf(keys, sizeof(keys), "%s", fixed_keys);
            snprintf(label, sizeof(label), "%s", function);
        } else
            snprintf(label, sizeof(label), "%s",
                     s->keys_reset_confirm ? _("[Confirmar: restaurar todos os atalhos]")
                                           : _("[Restaurar todos os atalhos]"));
        if (s->keys_capture && row->kind == KEYS_ACTION && row->index == (int)s->keys_action)
            snprintf(keys, sizeof(keys), "%s", _("pressione uma combinação"));
        if (changed)
            text_label(w, y, 2, 2, "•");
        text_label(w, y, 4, width - keys_width - 8, label);
        if (!focused && fixed)
            wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        int keys_x = width - keys_width - 2;
        text_label(w, y, keys_x, keys_width, keys);
    }
    if (s->keys_menu)
        keys_menu_draw(s, w, width, menu_row);
}

/* The hint below the list, unless an error takes the line. */
static const char *keys_hint(app_settings *s, char *buffer, size_t size)
{
    if (s->keys_capture) {
        snprintf(buffer, size, _("Pressione a nova tecla de \"%s\". Esc cancela."),
                 keymap_action_label(s->keys_action));
        return buffer;
    }
    if (s->ptt_capture)
        return _("Pressione a combinação completa; Ctrl/Alt sozinhos não contam. Esc cancela. "
                 "Mouse 4/5.");
    if (s->keys_conflict != KEYMAP_NONE) {
        char label[64] = "";
        key_binding_label(&s->keys_pending, label, sizeof(label));
        snprintf(buffer, size, _("%s já é usada por \"%s\". Enter substitui, Esc cancela."), label,
                 keymap_action_label(s->keys_conflict));
        return buffer;
    }
    if (s->keys_reset_confirm)
        return _("Enter restaura as teclas padrão de todas as funções; Esc cancela.");
    if (s->keys_menu)
        return _("↑↓ escolhe · Enter confirma · Esc fecha o menu");
    return _("Enter altera a função selecionada · digite para buscar");
}

/* Keys while the Atalhos content has focus. Esc first closes a menu, a
 * confirmation or a pending replacement; true when the key was used. */
static bool keys_key(app_settings *s, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    bool escape = type == OK && key == 27;
    if (s->keys_conflict != KEYMAP_NONE) {
        if (enter)
            keys_replace(s);
        else if (escape)
            s->keys_conflict = KEYMAP_NONE;
        return true;
    }
    if (s->keys_reset_confirm) {
        if (enter) {
            keymap_table_defaults(&s->keys);
            s->error[0] = 0;
        }
        if (enter || escape || type == OK || type == KEY_CODE_YES)
            s->keys_reset_confirm = false;
        return true;
    }
    if (s->keys_menu) {
        if (escape)
            s->keys_menu = false;
        else if (type == KEY_CODE_YES && key == KEY_UP && s->keys_menu_item)
            --s->keys_menu_item;
        else if (type == KEY_CODE_YES && key == KEY_DOWN && s->keys_menu_item + 1 < KEYS_MENU_ITEMS)
            ++s->keys_menu_item;
        else if (enter)
            keys_menu_activate(s);
        return true;
    }
    if (escape)
        return false;
    if (!s->keys_list) {
        if (type == KEY_CODE_YES && key == KEY_UP) {
            s->focus = 0;
            return true;
        }
        if (enter || (type == KEY_CODE_YES && key == KEY_DOWN)) {
            s->keys_list = true;
            s->keys_selection = 0;
            keys_move(s, 0);
            return true;
        }
        text_buffer before = s->keys_search;
        if (text_edit(&s->keys_search, type, key, 1024, false)) {
            if (before.length != s->keys_search.length ||
                memcmp(before.data, s->keys_search.data,
                       s->keys_search.length * sizeof(*s->keys_search.data)))
                s->keys_selection = s->keys_first = 0;
            return true;
        }
        return false;
    }
    keys_row rows[KEYS_ROWS];
    int count = keys_rows(s, rows);
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN))
        keys_move(s, key == KEY_UP ? -1 : 1);
    else if (type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_NPAGE)) {
        int step = key == KEY_PPAGE ? -8 : 8, target = s->keys_selection + step;
        s->keys_selection = target < 0 ? 0 : target >= count ? count - 1 : target;
        keys_clamp(s, rows, count);
    } else if (type == KEY_CODE_YES && (key == KEY_HOME || key == KEY_END)) {
        s->keys_selection = key == KEY_HOME ? 0 : count - 1;
        keys_clamp(s, rows, count);
    } else if (enter || (type == OK && key == ' '))
        keys_activate(s);
    else if ((type == OK && (key >= 32 || key == 8 || key == 21)) ||
             (type == KEY_CODE_YES && key == KEY_BACKSPACE)) {
        /* Typing, erasing and Ctrl+U search from the list as well. */
        s->keys_list = false;
        return keys_key(s, type, key);
    } else
        return false;
    return true;
}

static void keys_click(app_settings *s, int y, int x)
{
    s->focus = 1;
    if (s->keys_menu) {
        int item = y - s->keys_menu_y - 1;
        if (x >= s->keys_menu_x && x < s->keys_menu_x + 30 && item >= 0 && item < KEYS_MENU_ITEMS) {
            s->keys_menu_item = item;
            keys_menu_activate(s);
        } else
            s->keys_menu = false;
        return;
    }
    if (y == 5) {
        s->keys_list = false;
        return;
    }
    keys_row rows[KEYS_ROWS];
    int count = keys_rows(s, rows), index = s->keys_first + y - 6;
    if (index < 0 || index >= count || !keys_selectable(&rows[index]))
        return;
    s->keys_list = true;
    s->keys_selection = index;
    keys_activate(s);
}

static void tabs_draw(app_settings *s, WINDOW *w, int width)
{
    const char *names[SETTINGS_SECTIONS];
    for (int i = 0; i < SETTINGS_SECTIONS; ++i)
        names[i] = _(sections[i]);
    tab_strip_draw(&s->tabs, w, 3, width, names, SETTINGS_SECTIONS, s->section, s->focus == 0);
}
/* Perfil: items 0-3 are the text fields, then status, avatar and banner. */
#define PROFILE_ITEMS 7
enum { ITEM_STATUS = PROFILE_FIELDS, ITEM_AVATAR, ITEM_BANNER };
static const char *const field_labels[PROFILE_FIELDS] = {N_("Apelido"), N_("Frase de status"),
                                                         N_("Descrição"), N_("Frase de digitação")};
static const char *const field_keys[PROFILE_FIELDS] = {"nickname", "status", "description",
                                                       "typing"};
/* The status phrase is read back from the profile as status_message. */
static const char *const profile_keys[PROFILE_FIELDS] = {"nickname", "status_message",
                                                         "description", "typing"};
static const size_t field_max[PROFILE_FIELDS] = {PROFILE_NICKNAME_MAX, PROFILE_STATUS_MAX,
                                                 PROFILE_DESCRIPTION_MAX, PROFILE_TYPING_MAX};
static const char *const status_labels[3] = {N_("Online"), N_("Ausente"), N_("Ocupado")};
static const char *const status_values[3] = {NULL, "away", "busy"};

static const char *profile_text(const json_t *profile, const char *key)
{
    const char *value = json_string_value(json_object_get(profile, key));
    return value ? value : "";
}
static int profile_status(const json_t *profile)
{
    const char *status = profile_text(profile, "status");
    return !strcmp(status, "away") ? 1 : !strcmp(status, "busy") ? 2 : 0;
}
bool settings_poll(app_settings *s)
{
    if (!s->open || !s->profile)
        return false;
    if (s->section == SETTINGS_PROFILE && !s->requested) {
        /* Fetched fresh each time the panel shows it: another client may
         * have changed the profile since the last look. */
        s->requested = true;
        history_profile_load(s->profile, true);
    }
    const json_t *profile = history_profile(s->profile);
    uint64_t loads = history_profile_loads(s->profile);
    /* A load answered while a change is pending predates it; the model's copy
     * is updated on confirmation, so refill once nothing is pending. */
    bool refill = profile && loads != s->filled_loads && !s->dirty &&
                  history_profile_state(s->profile) != PROFILE_SAVING;
    if (refill) {
        for (int i = 0; i < PROFILE_FIELDS; ++i) {
            if (!text_decode(&s->fields[i], profile_text(profile, profile_keys[i])))
                text_clear(&s->fields[i]);
            s->field_first[i] = 0;
        }
        s->status_choice = s->loaded_status = profile_status(profile);
        s->filled_loads = loads;
    }
    uint64_t revision = history_profile_revision(s->profile);
    bool changed = refill || revision != s->seen_revision;
    s->seen_revision = revision;
    unsigned seen = s->auth ? (unsigned)auth_password_state(s->auth) * 4u +
                                  (history_password_waiting(s->profile) ? 2u : 0u) +
                                  (history_password_not_started(s->profile) ? 1u : 0u)
                            : 0u;
    changed |= seen != s->account_seen;
    s->account_seen = seen;
    uint64_t user_revision = history_user_settings_revision(s->profile);
    if (user_revision != s->user_revision) {
        s->user_revision = user_revision;
        changed = true;
        user_settings_state state = history_user_settings_state(s->profile);
        bool edited = !user_config_equal(&s->user, &s->user_base);
        /* An untouched draft follows the server, including changes made on
         * another device; a confirmed save adopts what the server kept. A
         * refused or unconfirmed one keeps the draft for another try. */
        if (!history_user_settings_busy(s->profile) &&
            (!edited || (s->user_sent && state == USER_SETTINGS_SAVED)))
            s->user = s->user_base = *history_user_config(s->profile);
        if (state != USER_SETTINGS_SAVING)
            s->user_sent = false;
    }
    return changed;
}
/* Aparência: the modes and language of this device, then the account's theme and display. */
enum {
    ITEM_LANGUAGE = 3,
    ITEM_THEME,
    ITEM_DENSITY,
    ITEM_TIMESTAMPS,
    ITEM_AVATARS,
    APPEARANCE_ITEMS
};
static const char *const theme_labels[] = {N_("Escuro"), N_("Claro"), N_("Sistema")};
static const char *const density_labels[] = {N_("Compacta"), N_("Normal"), N_("Confortável")};
/* Account items need the active server's model. */
static int appearance_last(const app_settings *s)
{
    return s->profile ? APPEARANCE_ITEMS - 1 : ITEM_LANGUAGE;
}
static void appearance_label(app_settings *s, int item, char *label, size_t size)
{
    switch (item) {
    case ITEM_LANGUAGE:
        snprintf(label, size, _("Idioma: < %s >"), language_choice_label(s->language));
        break;
    case ITEM_THEME:
        snprintf(label, size, _("Tema: < %s >"), _(theme_labels[s->user.theme]));
        break;
    case ITEM_DENSITY:
        snprintf(label, size, _("Densidade: < %s >"), _(density_labels[s->user.density]));
        break;
    case ITEM_TIMESTAMPS:
        snprintf(label, size, _("[%c] Mostrar horários"), s->user.timestamps ? 'x' : ' ');
        break;
    case ITEM_AVATARS:
        snprintf(label, size, _("[%c] Mostrar avatares"), s->user.avatars ? 'x' : ' ');
        break;
    default:
        snprintf(label, size, "(%c) %s", s->draft == (app_mode)item ? '*' : ' ',
                 item == 0   ? _("Normal")
                 : item == 1 ? _("Light")
                             : _("Ultralight"));
    }
}
static const char *appearance_description(app_settings *s)
{
    if (s->selection < ITEM_LANGUAGE)
        return app_mode_description((app_mode)(s->focus == 1 ? s->selection : (int)s->draft));
    if (s->selection == ITEM_LANGUAGE)
        return _("Idioma dos textos do aplicativo neste computador. Sistema segue o idioma do "
                 "sistema, ou PAPO_LANGUAGE (pt ou en) quando definida. Mensagens e nomes "
                 "continuam como foram escritos.");
    /* Account items are saved on the server and follow the active tab. */
    if (s->selection == ITEM_THEME)
        return _("Cores da tela. Sistema usa o tema claro quando o fundo do terminal é claro. "
                 "Salvo na sua conta deste servidor.");
    if (s->selection == ITEM_DENSITY)
        return _("Compacta tira a linha em branco entre autores; Confortável também separa as "
                 "mensagens de um mesmo autor. Salvo na sua conta deste servidor.");
    if (s->selection == ITEM_TIMESTAMPS)
        return _("Mostra a hora ao lado do nome de quem enviou cada mensagem. Salvo na sua conta "
                 "deste servidor.");
    return _("Mostra a foto, ou as iniciais, à esquerda das mensagens; sem ela, o texto começa "
             "na borda. Salvo na sua conta deste servidor.");
}
/* Rows available for the list, which scrolls on short terminals. */
static int appearance_rows(int height)
{
    int rows = height - 8 - (height >= 16 ? 3 : 0);
    return rows < 1 ? 1 : rows;
}
static void appearance_draw(app_settings *s, WINDOW *w, int height, int width)
{
    int first = first_mode(s), last = appearance_last(s), rows = appearance_rows(height);
    if (s->selection > last)
        s->selection = last;
    if (s->appearance_first < first)
        s->appearance_first = first;
    if (s->selection < s->appearance_first)
        s->appearance_first = s->selection;
    if (s->selection >= s->appearance_first + rows)
        s->appearance_first = s->selection - rows + 1;
    char label[160];
    int row = 0;
    for (int item = s->appearance_first; item <= last && row < rows; ++item, ++row) {
        bool focused = s->focus == 1 && s->selection == item;
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                        (focused ? theme_focus_attributes() : 0));
        appearance_label(s, item, label, sizeof(label));
        text_label(w, 5 + row, 2, width - 4, label);
    }
    if (height < 16)
        return;
    text_buffer description;
    text_layout measured;
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (text_decode(&description, appearance_description(s))) {
        text_measure_words(&description, width - 6, &measured);
        text_draw(w, &description, &measured, 0, 3, 6 + row, 3);
    }
}
static void cycle(int *value, int count, int direction)
{
    *value = (*value + (direction < 0 ? count - 1 : 1)) % count;
}
/* Changes the item under selection; modes are only chosen, never cycled. */
static void appearance_adjust(app_settings *s, int direction, bool choose)
{
    int value;
    switch (s->selection) {
    case ITEM_LANGUAGE:
        value = (int)s->language;
        cycle(&value, 3, direction);
        s->language = (language_choice)value;
        break;
    case ITEM_THEME:
        value = (int)s->user.theme;
        cycle(&value, 3, direction);
        s->user.theme = (user_theme)value;
        break;
    case ITEM_DENSITY:
        value = (int)s->user.density;
        cycle(&value, 3, direction);
        s->user.density = (user_density)value;
        break;
    case ITEM_TIMESTAMPS:
        s->user.timestamps = !s->user.timestamps;
        break;
    case ITEM_AVATARS:
        s->user.avatars = !s->user.avatars;
        break;
    default:
        if (choose)
            s->draft = (app_mode)s->selection;
        return;
    }
    s->error[0] = 0;
}
static void appearance_key(app_settings *s, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == KEY_CODE_YES && key == KEY_UP) {
        if (s->selection <= first_mode(s))
            s->focus = 0;
        else
            --s->selection;
    } else if (type == KEY_CODE_YES && key == KEY_DOWN) {
        if (s->selection >= appearance_last(s))
            s->focus = 2;
        else
            ++s->selection;
    } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
        if (s->selection >= ITEM_LANGUAGE)
            appearance_adjust(s, key == KEY_LEFT ? -1 : 1, false);
    } else if (enter || (type == OK && key == ' '))
        appearance_adjust(s, 1, true);
}
static void appearance_click(app_settings *s, int y, int x)
{
    int item = s->appearance_first + y - 5;
    if (y < 5 || y >= 5 + appearance_rows(getmaxy(s->window)) || item > appearance_last(s))
        return;
    s->selection = item;
    s->focus = 1;
    appearance_adjust(s, x < getmaxx(s->window) / 2 ? -1 : 1, true);
}
/* Acessibilidade: contrast and colors of this device. */
#define ACCESS_ITEMS 12
static void access_label(const app_settings *s, int item, char *label, size_t size)
{
    static const char *contrasts[] = {N_("Padrão"), N_("Alto"), N_("Sem cores")};
    static const char *roles[] = {N_("Originais"), N_("Contraste ajustado"), N_("Desativadas")};
    static const char *alerts[] = {N_("Desligado"), N_("Destaque"), N_("Piscar")};
    static const char *notices[] = {N_("2 segundos"), N_("6 segundos"), N_("Até uma tecla")};
    switch (item) {
    case 0:
        snprintf(label, size, _("Contraste: < %s >"), _(contrasts[s->access.contrast]));
        break;
    case 1:
        snprintf(label, size, "[%c] %s", s->access.colorblind ? 'x' : ' ',
                 _("Cores para daltonismo"));
        break;
    case 2:
        snprintf(label, size, "[%c] %s", s->access.cues ? 'x' : ' ', _("Indicadores além da cor"));
        break;
    case 3:
        snprintf(label, size, _("Cores dos cargos: < %s >"), _(roles[s->access.roles]));
        break;
    case 4:
        snprintf(label, size, "[%c] %s", s->access.screen_reader ? 'x' : ' ',
                 _("Modo leitor de tela"));
        break;
    case 5:
        snprintf(label, size, _("Símbolos da interface: < %s >"),
                 s->access.text_symbols ? _("Texto") : _("Ícones"));
        break;
    case 6:
        snprintf(label, size, _("Emojis nas mensagens: < %s >"),
                 s->access.emoji_names ? _("Nomes") : _("Imagens"));
        break;
    case 7:
        snprintf(label, size, _("Bordas: < %s >"),
                 s->access.ascii_borders ? _("ASCII") : _("Linhas"));
        break;
    case 8:
        snprintf(label, size, _("Alerta visual: < %s >"), _(alerts[s->access.visual_alert]));
        break;
    case 9:
        snprintf(label, size, _("Duração dos avisos: < %s >"), _(notices[s->access.notices]));
        break;
    case 10:
        snprintf(label, size, _("Mouse: < %s >"),
                 s->access.mouse_off ? _("Desativado") : _("Ativado"));
        break;
    default:
        snprintf(label, size, "[%c] %s", s->access.confirm_exit ? 'x' : ' ',
                 _("Confirmar ações que encerram"));
        break;
    }
}
static const char *access_description(const app_settings *s)
{
    static const char *items[ACCESS_ITEMS] = {
        N_("Alto usa fundo preto ou branco puro, textos e bordas mais fortes. Sem cores usa só as "
           "cores do terminal e marca o foco em vídeo reverso; NO_COLOR também desliga as cores."),
        N_("Erros e o ponto de não lidas ficam laranja; sucesso, presença e quem fala ficam azuis, "
           "em vez de vermelho e verde."),
        N_("O item com foco também fica em negrito e sublinhado, para que o foco não dependa só da "
           "cor."),
        N_("Ajustado clareia ou escurece a cor de cada cargo até ler bem sobre o fundo; "
           "desativadas mostra todos os nomes na cor do texto."),
        N_("O cursor do terminal acompanha o item com foco, para leitores de tela como Orca e "
           "NVDA; a animação de carregamento para e os ícones viram texto."),
        N_("Texto troca os ícones da interface, como 🔍, ⚙ e 📎, por rótulos como [/], [*] e "
           "[+]."),
        N_("Nomes mostra cada emoji como :nome:, inclusive os do servidor, em vez de imagens."),
        N_("ASCII desenha bordas e linhas com + - |, que leitores de tela e fontes limitadas leem "
           "melhor."),
        N_("Destaque mostra cada nova notificação no rodapé, mesmo sem som; Piscar também pisca a "
           "tela uma vez, no máximo a cada 3 segundos."),
        N_("Quanto tempo ficam os avisos que somem sozinhos, como o de envio confirmado e o do "
           "alerta visual."),
        N_("Desativado deixa o mouse com o terminal, para selecionar texto como em qualquer "
           "programa; tudo continua acessível pelo teclado."),
        N_("Sair do aplicativo, sair da conta e fechar uma DM pedem a mesma tecla de novo, sem "
           "limite de tempo; outra tecla cancela.")};
    return _(
        items[s->access_selection >= 0 && s->access_selection < ACCESS_ITEMS ? s->access_selection
                                                                             : 0]);
}
/* Items shown at once: three rows stay for the explanation of the selected
 * item and one for the note about font and density. */
static int access_rows(int height)
{
    int rows = height - 13;
    return rows < 1 ? 1 : rows > ACCESS_ITEMS ? ACCESS_ITEMS : rows;
}
static void access_draw(app_settings *s, WINDOW *w, int height, int width)
{
    int visible = access_rows(height);
    if (s->access_selection < s->access_first)
        s->access_first = s->access_selection;
    if (s->access_selection >= s->access_first + visible)
        s->access_first = s->access_selection - visible + 1;
    for (int row = 0; row < visible && s->access_first + row < ACCESS_ITEMS; ++row) {
        int item = s->access_first + row;
        char label[256];
        bool focused = s->focus == 1 && s->access_selection == item;
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                        (focused ? theme_focus_attributes() : 0));
        access_label(s, item, label, sizeof(label));
        text_label(w, 5 + row, 2, width - 4, label);
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_buffer description;
    text_layout measured;
    int rows = height - 5 - (5 + visible);
    if (rows > 0 && text_decode(&description, access_description(s))) {
        text_measure_words(&description, width - 6, &measured);
        text_draw(w, &description, &measured, 0, rows, 5 + visible, 3);
    }
    text_label(w, height - 4, 2, width - 4,
               _("A fonte é a do terminal; a densidade do histórico fica em Aparência."));
}
static void access_adjust(app_settings *s, int direction)
{
    s->error[0] = 0;
    switch (s->access_selection) {
    case 0: {
        int contrast = (int)s->access.contrast;
        cycle(&contrast, 3, direction);
        s->access.contrast = (access_contrast)contrast;
        break;
    }
    case 1:
        s->access.colorblind = !s->access.colorblind;
        break;
    case 2:
        s->access.cues = !s->access.cues;
        break;
    case 3: {
        int roles = (int)s->access.roles;
        cycle(&roles, 3, direction);
        s->access.roles = (access_roles)roles;
        break;
    }
    case 4:
        s->access.screen_reader = !s->access.screen_reader;
        break;
    case 5:
        s->access.text_symbols = !s->access.text_symbols;
        break;
    case 6:
        s->access.emoji_names = !s->access.emoji_names;
        break;
    case 7:
        s->access.ascii_borders = !s->access.ascii_borders;
        break;
    case 8: {
        int alert = (int)s->access.visual_alert;
        cycle(&alert, 3, direction);
        s->access.visual_alert = (access_alert)alert;
        break;
    }
    case 9: {
        int notices = (int)s->access.notices;
        cycle(&notices, 3, direction);
        s->access.notices = (access_notice)notices;
        break;
    }
    case 10:
        s->access.mouse_off = !s->access.mouse_off;
        break;
    default:
        s->access.confirm_exit = !s->access.confirm_exit;
        break;
    }
}
static void access_key(app_settings *s, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == KEY_CODE_YES && key == KEY_UP) {
        if (!s->access_selection)
            s->focus = 0;
        else
            --s->access_selection;
    } else if (type == KEY_CODE_YES && key == KEY_DOWN) {
        if (s->access_selection == ACCESS_ITEMS - 1)
            s->focus = 2;
        else
            ++s->access_selection;
    } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
        access_adjust(s, key == KEY_LEFT ? -1 : 1);
    else if (enter || (type == OK && key == ' '))
        access_adjust(s, 1);
}
static void access_click(app_settings *s, int y, int x)
{
    int item = s->access_first + y - 5;
    if (y < 5 || y >= 5 + access_rows(getmaxy(s->window)) || item >= ACCESS_ITEMS)
        return;
    s->access_selection = item;
    s->focus = 1;
    access_adjust(s, x < getmaxx(s->window) / 2 ? -1 : 1);
}
/* Draws text wrapped into at most rows rows from y and returns the rows it took.
 * Paths have no spaces, so they wrap anywhere; prose wraps between words. */
static int paragraph(WINDOW *w, const char *text, bool words, int y, int x, int width, int rows)
{
    text_buffer buffer;
    text_layout layout;
    if (rows < 1 || width < 1 || !text_decode(&buffer, text) || !buffer.length)
        return 0;
    if (words)
        text_measure_words(&buffer, width, &layout);
    else
        text_measure(&buffer, width, &layout);
    text_draw(w, &buffer, &layout, 0, rows, y, x);
    return layout.rows < rows ? layout.rows : rows;
}
/* Why the file is or is not being saved, from the last load. */
static void system_status(const app_settings *s, char *status, size_t size)
{
    if (s->file_writable)
        snprintf(status, size, "%s",
                 s->file_existed
                     ? _("Em uso. As alterações deste computador são salvas neste arquivo.")
                     : _("Ainda não existe. Será criado na primeira alteração salva."));
    else if (s->file_state == PROFILE_FILE_NEWER)
        snprintf(status, size,
                 _("Bloqueado: foi gravado por uma versão mais nova do Papo TUI (formato %d; "
                   "esta lê até o %d). Nada é salvo até você usar uma versão que leia esse "
                   "formato."),
                 s->file_version, PROFILE_FILE_VERSION);
    else if (s->file_state == PROFILE_FILE_INVALID)
        snprintf(status, size, "%s",
                 _("Bloqueado: o arquivo está corrompido ou não é uma configuração válida. Nada "
                   "é salvo até você corrigi-lo, ou movê-lo para outro lugar, e reiniciar o "
                   "aplicativo."));
    else if (s->file_state == PROFILE_FILE_NO_DIRECTORY)
        snprintf(status, size, "%s",
                 _("Indisponível: a pasta de configuração não pôde ser aberta como uma pasta "
                   "privada sua. Nada é salvo nesta sessão."));
    else
        snprintf(status, size, "%s",
                 _("Bloqueado: o arquivo não pôde ser lido como um arquivo privado seu de até "
                   "256 KiB. Corrija-o e reinicie o aplicativo."));
}
/* Sistema: where this device's settings are kept and whether they can be saved.
 * Nothing is edited here; the button copies the path. The description leads
 * with what the file never holds, so a short window cuts only the end of the list. */
static void system_draw(app_settings *s, WINDOW *w, int height, int width)
{
    int row = 5, end = height - 3;
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    text_label(w, row++, 2, width - 4, _("Arquivo de configuração:"));
    int rows = *s->config_path
                   ? paragraph(w, s->config_path, false, row, 4, width - 7, MIN(3, end - row))
                   : 0;
    if (!rows && row < end) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, row, 4, width - 7, _("Local indisponível."));
        rows = 1;
    }
    row += rows + 1;
    char status[512];
    system_status(s, status, sizeof(status));
    wattrset(w, COLOR_PAIR(s->file_writable ? THEME_PANEL : THEME_ERROR));
    row += paragraph(w, status, true, row, 2, width - 4, MIN(3, end - row)) + 1;
    if (height < 16)
        return;
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    paragraph(w,
              _("Senhas e cookies de sessão não são gravados neste arquivo. Ele guarda o modo do "
                "aplicativo, o idioma, o áudio, os atalhos, a acessibilidade, as preferências "
                "deste computador e os servidores salvos."),
              true, row, 3, width - 6, end - row);
}
/* Notificações: the account's switches; this device keeps its own in Ctrl+D. */
#define NOTIFY_ITEMS 4
static bool *notify_value(app_settings *s, int item)
{
    return item == 0   ? &s->user.notifications
           : item == 1 ? &s->user.preview
           : item == 2 ? &s->user.sound
                       : &s->user.mentions;
}
static void notifications_draw(app_settings *s, WINDOW *w, int height, int width)
{
    static const char *const labels[NOTIFY_ITEMS] = {N_("Notificações ativadas"),
                                                     N_("Prévia da mensagem"), N_("Sons"),
                                                     N_("Avisar quando mencionarem você")};
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    char label[256];
    snprintf(label, sizeof(label), _("Como você é avisado em %s"),
             *s->server ? s->server : _("este servidor"));
    text_label(w, 5, 2, width - 4, label);
    if (!s->profile) {
        text_label(w, 7, 2, width - 4, _("Indisponível sem um servidor conectado."));
        return;
    }
    for (int i = 0; i < NOTIFY_ITEMS && 7 + i < height - 4; ++i) {
        bool focused = s->focus == 1 && s->notify_selection == i;
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                        (focused ? theme_focus_attributes() : 0));
        snprintf(label, sizeof(label), "[%c] %s", *notify_value(s, i) ? 'x' : ' ', _(labels[i]));
        text_label(w, 7 + i, 2, width - 4, label);
    }
    if (height < 18)
        return;
    /* What the selected switch does; the scope of all of them otherwise. */
    static const char *const help[NOTIFY_ITEMS] = {
        N_("Mostra avisos do sistema e toca sons para as notificações deste servidor. Desligado, "
           "a central ({k1:conversation.notifications}) e os indicadores de não lidas continuam "
           "funcionando."),
        N_("Mostra o texto da mensagem no aviso do sistema. Desligado, o aviso diz só \"Nova "
           "notificação\"."),
        N_("Toca um som quando chega uma notificação e quando você ou outra pessoa entra ou sai "
           "de uma chamada de voz."),
        N_("Avisa quando alguém menciona você, responde sua mensagem ou usa @everyone. "
           "Desligado, essas mensagens vão só para a central ({k1:conversation.notifications}), "
           "sem aviso nem som."),
    };
    text_buffer description;
    text_layout measured;
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (text_decode(&description, s->focus == 1
                                      ? K_(help[s->notify_selection])
                                      : K_("Valem para todos os canais deste servidor, em qualquer "
                                           "aparelho. Quais mensagens avisam se escolhe no canal "
                                           "(Notificações…); {k1:conversation.notifications} → "
                                           "Preferências silencia só este "
                                           "computador."))) {
        text_measure_words(&description, width - 6, &measured);
        text_draw(w, &description, &measured, 0, 3, 12, 3);
    }
}
static void notifications_key(app_settings *s, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == KEY_CODE_YES && key == KEY_UP) {
        if (!s->notify_selection)
            s->focus = 0;
        else
            --s->notify_selection;
    } else if (type == KEY_CODE_YES && key == KEY_DOWN) {
        if (!s->profile || s->notify_selection == NOTIFY_ITEMS - 1)
            s->focus = 2;
        else
            ++s->notify_selection;
    } else if (s->profile && (enter || (type == OK && key == ' ') ||
                              (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)))) {
        bool *value = notify_value(s, s->notify_selection);
        *value = !*value;
        s->error[0] = 0;
    }
}
static void user_notice_draw(app_settings *s, WINDOW *w, int height, int width)
{
    user_settings_state state = history_user_settings_state(s->profile);
    const char *notice = history_user_settings_notice(s->profile);
    if (!*notice)
        return;
    wattrset(w, COLOR_PAIR(state == USER_SETTINGS_FAILED || state == USER_SETTINGS_UNCERTAIN
                               ? THEME_ERROR
                           : state == USER_SETTINGS_SAVED ? THEME_SUCCESS
                                                          : THEME_PANEL_MUTED));
    text_label(w, height - 3, 2, width - 4, notice);
}
/* Queues the account draft; false when there is nothing to send. */
static bool user_save(app_settings *s)
{
    if (!s->profile || user_config_equal(&s->user, history_user_config(s->profile)))
        return false;
    if (history_user_settings_busy(s->profile)) {
        snprintf(s->error, sizeof(s->error), _("Aguarde a alteração anterior terminar."));
        return true;
    }
    if (!history_user_settings_save(s->profile, &s->user)) {
        snprintf(s->error, sizeof(s->error), _("Sem conexão com o servidor; tente novamente."));
        return true;
    }
    s->error[0] = 0;
    s->user_sent = true;
    return true;
}
static void profile_notice_draw(app_settings *s, WINDOW *w, int height, int width)
{
    profile_state state = history_profile_state(s->profile);
    const char *notice = history_profile_notice(s->profile);
    if (state == PROFILE_SAVING && !*notice)
        notice = _("Salvando…");
    if (!*notice)
        return;
    wattrset(w, COLOR_PAIR(state == PROFILE_FAILED || state == PROFILE_UNCERTAIN ? THEME_ERROR
                           : state == PROFILE_SAVED ? THEME_SUCCESS
                                                    : THEME_PANEL_MUTED));
    text_label(w, height - 3, 2, width - 4, notice);
}
/* The Perfil tab previews the saved avatar and banner as the member's profile
 * card shows them, above the fields. It needs a window taller than the usual
 * 20 rows: the avatar and a spacer take 4 more, and the banner takes the rest,
 * up to the rows its Discord-shaped band needs at the window's width. */
#define SETTINGS_BASE_HEIGHT 20
#define PREVIEW_MIN_HEIGHT 26
#define PREVIEW_AVATAR_ROWS 3
static int window_height(int section, int width)
{
    int base = LINES > SETTINGS_BASE_HEIGHT ? SETTINGS_BASE_HEIGHT : LINES - 2;
    int wanted = SETTINGS_BASE_HEIGHT + PREVIEW_AVATAR_ROWS + 1 + icons_banner_rows(width - 4, 99);
    int tall = MIN(wanted, LINES - 2);
    if (section != SETTINGS_PROFILE || app_mode_get() == APP_MODE_ULTRALIGHT ||
        tall < PREVIEW_MIN_HEIGHT)
        return base;
    return tall;
}
/* First letter of the name, standing in for an avatar that is not loaded. */
static void preview_initial(WINDOW *w, int y, int x, int width, int rows, const char *name)
{
    text_buffer text;
    if (!text_decode(&text, name) || !text.length)
        return;
    size_t first = 0;
    while (first < text.length && text.data[first] <= ' ')
        ++first;
    if (first == text.length)
        return;
    text.data[0] = text.data[first];
    text.length = 1;
    text.data[1] = 0;
    char initial[TEXT_UTF8_BYTES];
    if (text_encode(&text, initial)) {
        int columns = text_label(NULL, 0, 0, width, initial);
        text_label(w, y + (rows - 1) / 2, x + MAX(0, width - columns) / 2, columns, initial);
    }
}
/* Draws the preview in rows [y, y + rows). The pictures are placed by the
 * icons over the cells blanked here, so the letter and the "sem banner" label
 * show while an image is missing, loading or unavailable. */
static void preview_draw(app_settings *s, WINDOW *w, int y, int rows, int width,
                         const json_t *profile)
{
    int banner_rows = rows - PREVIEW_AVATAR_ROWS - 1;
    const char *banner = profile_text(profile, "banner_media");
    wattrset(w, COLOR_PAIR(THEME_INPUT));
    for (int r = 0; r < banner_rows; ++r)
        mvwhline(w, y + r, 2, ' ', width - 4);
    if (*banner)
        icons_member_picture(s->icons, banner, true, "settings:banner", getbegy(w) + y,
                             getbegx(w) + 2, width - 4, banner_rows, THEME_INPUT);
    else {
        wattrset(w, COLOR_PAIR(THEME_INPUT));
        text_label(w, y + (banner_rows - 1) / 2, 4, width - 8, _("sem banner"));
    }
    int ay = y + banner_rows;
    const char *nickname = profile_text(profile, "nickname"),
               *username = profile_text(profile, "username");
    const char *name = *nickname ? nickname : username;
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    for (int r = 0; r < PREVIEW_AVATAR_ROWS; ++r)
        mvwhline(w, ay + r, 2, ' ', 8);
    preview_initial(w, ay, 2, 8, PREVIEW_AVATAR_ROWS, name);
    const char *id = s->auth ? auth_user_id(s->auth) : NULL;
    if (id && *id)
        icons_member_picture(s->icons, id, false, "settings:avatar", getbegy(w) + ay,
                             getbegx(w) + 2, 8, PREVIEW_AVATAR_ROWS, THEME_PANEL);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, ay, 12, width - 14, name);
    char line[1100];
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (*username) {
        snprintf(line, sizeof(line), "@%s", username);
        text_label(w, ay + 1, 12, width - 14, line);
    }
    text_label(w, ay + 2, 12, width - 14, _(status_labels[profile_status(profile)]));
}
static void profile_draw(app_settings *s, WINDOW *w, int height, int width)
{
    s->cursor = false;
    for (int i = 0; i < PROFILE_ITEMS; ++i)
        s->item_y[i] = s->item_rows[i] = -1;
    char line[320];
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    snprintf(line, sizeof(line), _("Seu perfil em %s"),
             *s->server ? s->server : _("este servidor"));
    text_label(w, 5, 2, width - 4, line);
    const json_t *profile = history_profile(s->profile);
    if (!profile) {
        text_label(w, 7, 3, width - 6,
                   history_profile_state(s->profile) == PROFILE_FAILED
                       ? _("Não foi possível carregar o perfil. Enter tenta de novo.")
                       : _("Carregando perfil…"));
        return;
    }
    int preview = height > SETTINGS_BASE_HEIGHT ? height - SETTINGS_BASE_HEIGHT : 0;
    if (preview)
        preview_draw(s, w, 6, preview, width, profile);
    int top = 6 + preview, bottom = height - 3, available = bottom - top;
    int rows[PROFILE_ITEMS] = {1, 1, available >= 10 ? 3 : available >= 9 ? 2 : 1, 1, 1, 1, 1};
    if (s->profile_item < s->profile_first)
        s->profile_first = s->profile_item;
    for (;;) {
        int used = 0;
        for (int i = s->profile_first; i <= s->profile_item; ++i)
            used += rows[i];
        if (used <= available || s->profile_first >= s->profile_item)
            break;
        ++s->profile_first;
    }
    int label_width = 20, fx = 2 + label_width, fw = width - fx - 10;
    for (int i = s->profile_first, y = top; i < PROFILE_ITEMS && y + rows[i] <= bottom; ++i) {
        s->item_y[i] = y;
        s->item_rows[i] = rows[i];
        bool focused = s->focus == 1 && s->profile_item == i, refused = s->profile_bad == i + 1;
        wattrset(w, COLOR_PAIR(refused   ? THEME_NOTIFICATION
                               : focused ? THEME_PANEL_ACCENT
                                         : THEME_PANEL) |
                        (focused || refused ? A_BOLD : 0));
        snprintf(line, sizeof(line), "%s:",
                 i < PROFILE_FIELDS ? _(field_labels[i])
                 : i == ITEM_STATUS ? _("Status")
                 : i == ITEM_AVATAR ? _("Avatar")
                                    : _("Banner"));
        text_label(w, y, 2, label_width - 1, line);
        if (i < PROFILE_FIELDS) {
            text_buffer *field = &s->fields[i];
            /* The items follow one another, so the border is its two sides. */
            for (int r = 0; refused && r < rows[i]; ++r)
                input_border_draw(w, y + r, fx, fw, THEME_NOTIFICATION, false, false);
            wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_INPUT) |
                            (focused ? theme_focus_attributes() : 0));
            for (int r = 0; r < rows[i]; ++r)
                mvwhline(w, y + r, fx, ' ', fw);
            int cy, cx;
            if (rows[i] > 1) {
                text_layout layout;
                text_measure(field, fw, &layout);
                text_keep_cursor(field, &layout, rows[i]);
                text_draw(w, field, &layout, field->first_row, rows[i], y, fx);
                cy = y + layout.cursor_row - field->first_row;
                cx = fx + layout.cursor_column;
            } else {
                text_draw_field(w, field, &s->field_first[i], y, fx, fw, false);
                getyx(w, cy, cx);
            }
            if (focused) {
                s->cursor = true;
                s->cursor_y = getbegy(w) + cy;
                s->cursor_x = getbegx(w) + cx;
            }
            bool over = field->length > field_max[i];
            wattrset(w, COLOR_PAIR(over ? THEME_ERROR : THEME_PANEL_MUTED));
            snprintf(line, sizeof(line), "%zu/%zu", field->length, field_max[i]);
            text_label(w, y, fx + fw + 1, 8, line);
        } else if (i == ITEM_STATUS) {
            snprintf(line, sizeof(line), "< %s >", _(status_labels[s->status_choice]));
            button_draw(w, y, fx, 13, line, focused ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        } else if (i == ITEM_AVATAR) {
            button_draw(w, y, fx, 12, _("Alterar…"), focused ? BUTTON_FOCUSED : BUTTON_NORMAL,
                        BUTTON_PANEL);
        } else {
            bool has_banner = *profile_text(profile, "banner_media");
            button_draw(w, y, fx, 12, _("Alterar…"),
                        focused && !s->banner_button ? BUTTON_FOCUSED : BUTTON_NORMAL,
                        BUTTON_PANEL);
            if (has_banner)
                button_draw(w, y, fx + 14, 11, _("Remover"),
                            focused && s->banner_button ? BUTTON_FOCUSED : BUTTON_NORMAL,
                            BUTTON_PANEL);
            else {
                s->banner_button = 0;
                wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
                text_label(w, y, fx + 14, width - fx - 16, _("sem banner"));
            }
        }
        y += rows[i];
    }
}
bool settings_cursor(const app_settings *s)
{
    if (!s->open || (s->section != SETTINGS_PROFILE && s->section != SETTINGS_ACCOUNT) ||
        !s->cursor || !s->window)
        return false;
    setsyx(s->cursor_y, s->cursor_x);
    return true;
}
static bool account_locked(app_settings *s)
{
    return (s->profile && history_password_waiting(s->profile)) ||
           (s->auth && auth_password_state(s->auth) == AUTH_PASSWORD_PENDING);
}
bool settings_paste(app_settings *s, text_paste *paste)
{
    if (s->open && s->section == SETTINGS_ACCOUNT && s->focus == 1) {
        if (account_locked(s))
            text_paste_cancel(paste);
        else if (!text_paste_commit(paste, &s->account[s->account_item]))
            snprintf(s->error, sizeof(s->error), _("Colagem excede o limite do campo."));
        else
            s->error[0] = 0;
        s->account_confirm = false;
        return true;
    }
    if (!s->open || s->section != SETTINGS_PROFILE || s->focus != 1 ||
        s->profile_item >= PROFILE_FIELDS || !history_profile(s->profile))
        return false;
    if (s->profile_bad == s->profile_item + 1)
        s->profile_bad = 0;
    if (!text_paste_commit(paste, &s->fields[s->profile_item]))
        snprintf(s->error, sizeof(s->error), _("Colagem excede o limite do campo."));
    else
        s->error[0] = 0;
    s->dirty = true;
    return true;
}
static void profile_save(app_settings *s)
{
    const json_t *profile = history_profile(s->profile);
    if (!profile) {
        snprintf(s->error, sizeof(s->error), _("O perfil ainda não foi carregado."));
        return;
    }
    static char text[PROFILE_FIELDS][TEXT_UTF8_BYTES];
    bool changed[PROFILE_FIELDS], any = false;
    for (int i = 0; i < PROFILE_FIELDS; ++i) {
        const char *problem = text_encode(&s->fields[i], text[i])
                                  ? history_profile_check(field_keys[i], text[i])
                                  : _("Texto inválido.");
        if (problem) {
            snprintf(s->error, sizeof(s->error), "%s", problem);
            s->profile_item = i;
            s->profile_bad = i + 1;
            s->focus = 1;
            return;
        }
        changed[i] = strcmp(text[i], profile_text(profile, profile_keys[i])) != 0;
        any |= changed[i];
    }
    bool status = s->status_choice != s->loaded_status;
    if (!any && !status) {
        snprintf(s->error, sizeof(s->error), _("Nenhuma alteração para salvar."));
        return;
    }
    s->error[0] = 0;
    if (any &&
        !history_profile_save(s->profile, changed[0] ? text[0] : NULL, changed[1] ? text[1] : NULL,
                              changed[2] ? text[2] : NULL, changed[3] ? text[3] : NULL)) {
        snprintf(s->error, sizeof(s->error), _("Não foi possível preparar a alteração do perfil."));
        return;
    }
    if (status && !history_profile_set_status(s->profile, status_values[s->status_choice])) {
        snprintf(s->error, sizeof(s->error), _("Não foi possível preparar a mudança de status."));
        return;
    }
    s->loaded_status = s->status_choice;
    s->dirty = false;
}
static void profile_activate(app_settings *s, int item)
{
    if (item == ITEM_STATUS)
        s->status_choice = (s->status_choice + 1) % 3;
    else if (item == ITEM_AVATAR)
        s->pick = SETTINGS_PICK_AVATAR;
    else if (item == ITEM_BANNER) {
        if (!s->banner_button)
            s->pick = SETTINGS_PICK_BANNER;
        else if (!history_profile_remove_banner(s->profile))
            snprintf(s->error, sizeof(s->error), _("Não foi possível remover o banner agora."));
    }
}
static void profile_key(app_settings *s, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    bool up = type == KEY_CODE_YES && key == KEY_UP, down = type == KEY_CODE_YES && key == KEY_DOWN;
    int item = s->profile_item;
    if (!history_profile(s->profile)) {
        if (up)
            s->focus = 0;
        else if (down)
            s->focus = 2;
        else if (enter && history_profile_state(s->profile) == PROFILE_FAILED)
            history_profile_load(s->profile, true);
        return;
    }
    if (item < PROFILE_FIELDS) {
        text_buffer *field = &s->fields[item];
        if (enter) {
            profile_save(s);
            return;
        }
        if (item == PROFILE_FIELD_DESCRIPTION && (up || down)) {
            /* Arrows move between lines first, then to the next control. */
            text_layout layout;
            int width = s->window ? getmaxx(s->window) - 32 : 40;
            text_measure(field, width, &layout);
            if ((up && layout.cursor_row > 0) || (down && layout.cursor_row + 1 < layout.rows)) {
                text_edit(field, type, key, width, true);
                return;
            }
        }
        if (!up && !down) {
            if (text_edit(field, type, key, TEXT_LIMIT * 2 + 1,
                          item == PROFILE_FIELD_DESCRIPTION)) {
                s->dirty = true;
                s->error[0] = 0;
                if (s->profile_bad == item + 1)
                    s->profile_bad = 0;
            }
            return;
        }
    } else if (enter || (type == OK && key == ' ')) {
        profile_activate(s, item);
        return;
    } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
        if (item == ITEM_STATUS)
            s->status_choice = (s->status_choice + (key == KEY_LEFT ? 2 : 1)) % 3;
        else if (item == ITEM_BANNER && *profile_text(history_profile(s->profile), "banner_media"))
            s->banner_button = key == KEY_RIGHT;
        return;
    }
    if (up) {
        if (!item)
            s->focus = 0;
        else
            s->profile_item = item - 1;
    } else if (down) {
        if (item == PROFILE_ITEMS - 1)
            s->focus = 2;
        else
            s->profile_item = item + 1;
    }
}
static void profile_click(app_settings *s, int y, int x)
{
    for (int i = 0; i < PROFILE_ITEMS; ++i) {
        if (s->item_y[i] < 0 || y < s->item_y[i] || y >= s->item_y[i] + s->item_rows[i])
            continue;
        s->focus = 1;
        s->profile_item = i;
        int fx = 22;
        if (i == ITEM_BANNER && x >= fx + 14 && x < fx + 25)
            s->banner_button = 1;
        else if (i == ITEM_BANNER)
            s->banner_button = 0;
        if (i >= PROFILE_FIELDS && x >= fx)
            profile_activate(s, i);
        return;
    }
}
/* Conta: new password and confirmation, then the change after a confirmation. */
#define ACCOUNT_PASSWORD_MAX 1024
static const char *const account_labels[2] = {N_("Nova senha:"), N_("Confirmar senha:")};
static void account_draw(app_settings *s, WINDOW *w, int height, int width)
{
    s->cursor = false;
    s->account_y[0] = s->account_y[1] = -1;
    char line[1280];
    const char *user = s->auth ? auth_username(s->auth) : "";
    snprintf(line, sizeof(line), _("Sua conta em %s%s%s"),
             *s->server ? s->server : _("este servidor"), *user ? " · @" : "", user);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, 5, 2, width - 4, line);
    bool roomy = height >= 16, locked = account_locked(s);
    int label_width = 20, fx = 2 + label_width, fw = width - fx - 4;
    for (int i = 0; i < 2; ++i) {
        int y = roomy ? 7 + i * 2 : 6 + i;
        s->account_y[i] = y;
        bool focused = s->focus == 1 && s->account_item == i, refused = s->account_bad == i + 1;
        wattrset(w, COLOR_PAIR(refused   ? THEME_NOTIFICATION
                               : focused ? THEME_PANEL_ACCENT
                                         : THEME_PANEL) |
                        (focused || refused ? A_BOLD : 0));
        text_label(w, y, 2, label_width - 1, _(account_labels[i]));
        /* With room between the inputs the border is a box; otherwise its two sides. */
        if (refused)
            input_border_draw(w, y, fx, fw, THEME_NOTIFICATION, roomy, roomy);
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_INPUT) |
                        (focused ? theme_focus_attributes() : 0));
        mvwhline(w, y, fx, ' ', fw);
        text_draw_field(w, &s->account[i], &s->account_first[i], y, fx, fw, true);
        int cy, cx;
        getyx(w, cy, cx);
        if (focused && !locked) {
            s->cursor = true;
            s->cursor_y = getbegy(w) + cy;
            s->cursor_x = getbegx(w) + cx;
        }
    }
    int info = (roomy ? 9 : 7) + 2;
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (info < height - 3)
        text_label(w, info, 2, width - 4,
                   _("Use 8+ caracteres, uma letra maiúscula e um símbolo."));
    text_buffer warning;
    text_layout measured;
    if (info + 1 < height - 3 &&
        text_decode(&warning, _("Trocar a senha encerra todas as sessões desta conta, inclusive "
                                "esta: depois, entre de novo com a nova senha."))) {
        text_measure(&warning, width - 4, &measured);
        text_draw(w, &warning, &measured, 0, height - 3 - (info + 1), info + 1, 2);
    }
}
static void account_notice_draw(app_settings *s, WINDOW *w, int height, int width)
{
    const char *notice = NULL;
    int color = THEME_PANEL_MUTED;
    auth_password_outcome outcome = s->auth ? auth_password_state(s->auth) : AUTH_PASSWORD_NONE;
    if (s->profile && history_password_waiting(s->profile))
        notice = _("Aguardando para enviar a troca de senha…");
    else if (s->account_sent && s->profile && history_password_not_started(s->profile)) {
        notice = _("Não foi possível iniciar a troca de senha. Tente de novo.");
        color = THEME_ERROR;
    } else if (s->account_sent && outcome != AUTH_PASSWORD_NONE) {
        notice = auth_password_notice(s->auth);
        color = outcome == AUTH_PASSWORD_PENDING ? THEME_PANEL_MUTED : THEME_ERROR;
    } else if (s->account_confirm) {
        notice = _("Todas as sessões da conta serão encerradas. Escolha Confirmar para trocar.");
        color = THEME_PANEL_ACCENT;
    }
    if (!notice || !*notice)
        return;
    wattrset(w, COLOR_PAIR(color));
    text_label(w, height - 3, 2, width - 4, notice);
}
static void account_submit(app_settings *s)
{
    if (account_locked(s))
        return;
    if (!s->profile || !s->auth || !auth_has_session(s->auth)) {
        snprintf(s->error, sizeof(s->error), _("A sessão não está disponível."));
        return;
    }
    static char text[2][TEXT_UTF8_BYTES];
    char reason[192];
    bool encoded = text_encode(&s->account[0], text[0]) && text_encode(&s->account[1], text[1]);
    int item = -1;
    if (!encoded) {
        snprintf(s->error, sizeof(s->error), _("Senha inválida."));
        item = 0;
    } else if (s->account[0].length > ACCOUNT_PASSWORD_MAX) {
        snprintf(s->error, sizeof(s->error), _("A senha excedeu o limite do cliente."));
        item = 0;
    } else if (!auth_validate_password(text[0], reason, sizeof(reason))) {
        snprintf(s->error, sizeof(s->error), "%s", reason);
        item = 0;
    } else if (!s->account[1].length || strcmp(text[0], text[1])) {
        snprintf(s->error, sizeof(s->error), "%s",
                 s->account[1].length ? _("As senhas não coincidem.")
                                      : _("Confirme a nova senha."));
        item = 1;
    } else if (!s->account_confirm) {
        /* Ending every session is asked for twice. */
        s->account_confirm = true;
        s->account_sent = false;
        s->error[0] = 0;
        s->focus = 2;
    } else if (!history_password_change(s->profile, text[0]))
        snprintf(s->error, sizeof(s->error), _("Uma troca de senha já está em andamento."));
    else {
        s->account_sent = true;
        s->account_confirm = false;
        s->error[0] = 0;
        account_erase(s);
        s->account_item = 0;
    }
    if (item >= 0) {
        s->account_confirm = false;
        s->account_item = item;
        s->account_bad = item + 1;
        s->focus = 1;
    }
    memset(text, 0, sizeof(text));
}
static void account_key(app_settings *s, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (enter) {
        account_submit(s);
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_UP) {
        if (!s->account_item)
            s->focus = 0;
        else
            --s->account_item;
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_DOWN) {
        if (s->account_item == 1)
            s->focus = 2;
        else
            ++s->account_item;
        return;
    }
    if (account_locked(s))
        return;
    text_buffer *field = &s->account[s->account_item];
    if (text_edit(field, type, key, TEXT_LIMIT * 2 + 1, false)) {
        s->error[0] = 0;
        s->account_confirm = s->account_sent = false;
        if (s->account_bad == s->account_item + 1)
            s->account_bad = 0;
    }
}
static const char *about_button_label(void)
{
    switch (about_button_now()) {
    case ABOUT_BUTTON_DOWNLOAD:
        return _("Baixar atualização");
    case ABOUT_BUTTON_CANCEL:
        return _("Cancelar download");
    case ABOUT_BUTTON_COPY:
        return _("Copiar comando");
    case ABOUT_BUTTON_INSTALL:
        return _("Baixar e instalar");
    case ABOUT_BUTTON_APPLY:
        return _("Instalar e sair");
    default:
        return _("Verificar agora");
    }
}
void settings_draw(app_settings *s)
{
    if (!s->open)
        return;
    int width = COLS > 76 ? 76 : COLS - 2, height = window_height(s->section, width);
    if (width < 40 || height < 12)
        return;
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
    s->window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (!s->window)
        return;
    WINDOW *w = s->window;
    s->cursor = false;
    /* Pictures behind the panel are cut here; the preview drawn inside stays. */
    icons_overlay(s->icons, getbegy(w), getbegx(w), height, width);
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, _("Configurações"));
    tabs_draw(s, w, width);
    if (s->section == SETTINGS_APPEARANCE) {
        appearance_draw(s, w, height, width);
    } else if (s->section == SETTINGS_NOTIFICATIONS) {
        notifications_draw(s, w, height, width);
    } else if (s->section == SETTINGS_AUDIO) {
        audio_draw(s, w, height, width);
    } else if (s->section == SETTINGS_PROFILE) {
        profile_draw(s, w, height, width);
    } else if (s->section == SETTINGS_ACCOUNT) {
        account_draw(s, w, height, width);
    } else if (s->section == SETTINGS_SHORTCUTS) {
        keys_draw(s, w, height, width);
    } else if (s->section == SETTINGS_ACCESSIBILITY) {
        access_draw(s, w, height, width);
    } else if (s->section == SETTINGS_ABOUT) {
        about_draw(&s->about, w, 5, 2, width - 4, height - 8, s->focus == 1, s->update_check);
        /* The newer version is on screen: the footer need not repeat it. */
        update_acknowledge();
    } else if (s->section == SETTINGS_SYSTEM) {
        system_draw(s, w, height, width);
    }
    wattrset(w, COLOR_PAIR(THEME_ERROR));
    if (*s->error)
        text_label(w, height - 3, 2, width - 4, s->error);
    else if (s->section == SETTINGS_PROFILE)
        profile_notice_draw(s, w, height, width);
    else if (s->section == SETTINGS_ACCOUNT)
        account_notice_draw(s, w, height, width);
    else if (s->section == SETTINGS_SHORTCUTS) {
        char hint[512];
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, height - 3, 2, width - 4, keys_hint(s, hint, sizeof(hint)));
    } else if ((s->section == SETTINGS_ABOUT && s->about_copied) ||
               (s->section == SETTINGS_SYSTEM && s->path_copied)) {
        wattrset(w, COLOR_PAIR(THEME_SUCCESS));
        text_label(w, height - 3, 2, width - 4, _("Cópia solicitada."));
    } else if (s->profile &&
               (s->section == SETTINGS_APPEARANCE || s->section == SETTINGS_NOTIFICATIONS))
        user_notice_draw(s, w, height, width);
    bool profile = s->section == SETTINGS_PROFILE, account = s->section == SETTINGS_ACCOUNT,
         notifications = s->section == SETTINGS_NOTIFICATIONS, about = s->section == SETTINGS_ABOUT,
         system = s->section == SETTINGS_SYSTEM;
    button_draw(w, height - 2, 2, 18,
                profile         ? _("Salvar perfil")
                : account       ? s->account_confirm ? _("Confirmar") : _("Alterar senha")
                : notifications ? _("Salvar")
                : about         ? about_button_label()
                : system        ? _("Copiar caminho")
                                : _("Salvar e aplicar"),
                s->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, height - 2, width - 14, 12,
                profile || account || notifications || about || system ? _("Fechar")
                                                                       : _("Cancelar"),
                s->focus == 3 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
}
/* Sobre keeps no draft: each line acts at once and its button asks GitHub,
 * downloads the newer version or copies how to install it. */
static void about_act(app_settings *s, profile_config *config, about_action action)
{
    s->about_copied = false;
    s->error[0] = 0;
    if (action == ABOUT_COPY_SOURCE || action == ABOUT_COPY_RELEASE ||
        action == ABOUT_COPY_COMMAND) {
        s->about_copied = action == ABOUT_COPY_SOURCE    ? about_copy_source()
                          : action == ABOUT_COPY_RELEASE ? about_copy_release()
                                                         : about_copy_command();
        if (!s->about_copied)
            snprintf(s->error, sizeof(s->error), "%s",
                     action == ABOUT_COPY_COMMAND ? _("Não foi possível copiar o comando.")
                                                  : _("Não foi possível copiar o link."));
    } else if (action == ABOUT_TOGGLE_CHECK) {
        if (profiles_set_update_check(config, !s->update_check)) {
            s->update_check = config->update_check;
            update_set_automatic(s->update_check);
        } else
            snprintf(s->error, sizeof(s->error), "%s", config->message);
        file_snapshot(s, config);
    } else if (action == ABOUT_LEAVE_UP)
        s->focus = 0;
    else if (action == ABOUT_LEAVE_DOWN)
        s->focus = 2;
}
/* Sistema keeps no draft: its button copies the path of the configuration file. */
static void path_copy(app_settings *s)
{
    s->path_copied = *s->config_path && clipboard_copy_text(s->config_path);
    snprintf(s->error, sizeof(s->error), "%s",
             s->path_copied ? "" : _("Não foi possível copiar o caminho."));
}
static void save(app_settings *s, profile_config *config)
{
    if (s->section == SETTINGS_SYSTEM) {
        path_copy(s);
        return;
    }
    if (s->section == SETTINGS_ABOUT) {
        s->about_copied = false;
        s->error[0] = 0;
        switch (about_button_now()) {
        case ABOUT_BUTTON_DOWNLOAD:
            /* The screen owns the file picker: it asks where to save first. */
            s->pick = SETTINGS_PICK_UPDATE;
            break;
        case ABOUT_BUTTON_CANCEL:
            update_download_cancel();
            break;
        case ABOUT_BUTTON_COPY:
            about_act(s, config, ABOUT_COPY_COMMAND);
            break;
        case ABOUT_BUTTON_INSTALL:
            if (!update_install_start())
                snprintf(s->error, sizeof(s->error), "%s",
                         _("Não foi possível iniciar o download da atualização."));
            break;
        case ABOUT_BUTTON_APPLY:
            /* The screen leaves the program; the version is put in place after it. */
            s->quit = update_install_on_exit();
            break;
        default:
            if (!update_check_now() && update_status() != UPDATE_CHECKING)
                snprintf(s->error, sizeof(s->error), "%s",
                         _("Aguarde um minuto antes de verificar de novo."));
        }
        return;
    }
    if (s->section == SETTINGS_PROFILE) {
        profile_save(s);
        return;
    }
    if (s->section == SETTINGS_ACCOUNT) {
        account_submit(s);
        return;
    }
    if (s->section == SETTINGS_NOTIFICATIONS) {
        if (!user_save(s))
            snprintf(s->error, sizeof(s->error), _("Nenhuma alteração para salvar."));
        return;
    }
    keymap_overrides shortcuts;
    keymap_table_to_overrides(&s->keys, &config->shortcuts, &shortcuts);
    bool saved =
        profiles_set_settings(config, s->draft, s->language, &s->audio, &shortcuts, &s->access);
    file_snapshot(s, config);
    if (saved) {
        s->applied = true;
        s->original_audio = s->audio;
        voice_configure(&s->audio);
        /* The account part answers later: stay open to show its outcome. */
        if (!(s->section == SETTINGS_APPEARANCE && user_save(s)))
            settings_close(s);
    } else
        snprintf(s->error, sizeof(s->error), "%s", config->message);
}
static void cycle_device(app_settings *s, bool input, int direction)
{
    const json_t *devices = voice_devices(s->voice);
    const char *ids[129] = {""};
    size_t count = 1, selected = 0, i;
    json_t *device;
    char *target = input ? s->audio.input : s->audio.output;
    json_array_foreach(devices, i,
                       device) if (json_is_true(json_object_get(device, "input")) == input &&
                                   count < 129)
    {
        const char *id = json_string_value(json_object_get(device, "id"));
        if (!id)
            continue;
        if (!strcmp(target, id))
            selected = count;
        ids[count++] = id;
    }
    size_t next = (selected + (direction < 0 ? count - 1 : 1)) % count;
    snprintf(target, AUDIO_DEVICE_BYTES, "%s", ids[next]);
}
static void audio_adjust(app_settings *s, int direction)
{
    if (s->audio_selection == 0 || s->audio_selection == 1)
        cycle_device(s, s->audio_selection == 0, direction);
    else if (s->audio_selection == 2 || s->audio_selection == 3) {
        int *value =
            s->audio_selection == 2 ? &s->audio.microphone_volume : &s->audio.output_volume;
        *value += direction * 5;
        if (*value < 0)
            *value = 0;
        if (*value > 100)
            *value = 100;
    } else if (s->audio_selection == 4) {
        if (!voice_test(s->voice, !voice_testing()))
            snprintf(s->error, sizeof(s->error), _("Não foi possível iniciar o teste de áudio."));
        return;
    } else if (s->audio_selection == 5)
        s->audio.push_to_talk = !s->audio.push_to_talk;
    else if (s->audio_selection == 6) {
        begin_capture(s);
        return;
    } else if (s->audio_selection == 7) {
        unsigned slots = audio_config_slots(&s->audio, voice_instance(s->voice));
        if ((slots > 1 || direction > 0) && (slots < 64 || direction < 0))
            if (!audio_config_set_slots(&s->audio, voice_instance(s->voice), slots + direction))
                snprintf(s->error, sizeof(s->error),
                         _("Limite de configurações de instâncias atingido."));
    } else {
        voice_refresh_devices(s->voice);
        return;
    }
    voice_configure(&s->audio);
}
/* Push-to-talk takes its key during calls: never from quitting, help or the
 * settings, while another command of the conversation only gets a warning.
 * Either way the outcome is left in s->error. */
static bool ptt_allowed(app_settings *s, const key_binding *key)
{
    s->error[0] = 0;
    if (key->kind == KEY_BINDING_MOUSE)
        return true;
    key_binding wanted = *key;
    keymap_binding_normalize(&wanted);
    for (int i = 0; i < KEYMAP_ACTIONS; ++i) {
        keymap_action action = (keymap_action)i;
        if (!keymap_conversation_action(action))
            continue;
        for (unsigned j = 0; j < s->keys.count[i]; ++j) {
            if (!key_binding_equal(&s->keys.keys[i][j], &wanted))
                continue;
            char label[64] = "";
            key_binding_label(key, label, sizeof(label));
            if (keymap_action_flags(action) & KEYMAP_REQUIRED) {
                snprintf(s->error, sizeof(s->error), _("%s já é usada por \"%s\"; escolha outra."),
                         label, keymap_action_label(action));
                return false;
            }
            snprintf(s->error, sizeof(s->error),
                     _("Durante chamadas, %s transmite a voz em vez de \"%s\"."), label,
                     keymap_action_label(action));
            return true;
        }
    }
    return true;
}
void settings_key(app_settings *s, profile_config *config, int type, uint32_t key)
{
    if (!s->open)
        return;
    if (s->ptt_capture) {
        if (type == KEY_CODE_YES && key == TEXT_CAPTURED) {
            key_binding captured;
            if (text_capture_take(&captured)) {
                s->ptt_capture = false;
                if (key_binding_valid(&captured) && ptt_allowed(s, &captured)) {
                    s->audio.ptt = captured;
                    voice_configure(&s->audio);
                } else if (key_binding_valid(&captured)) {
                    /* ptt_allowed explained the refusal; capture again. */
                    char reason[192];
                    snprintf(reason, sizeof(reason), "%s", s->error);
                    begin_capture(s);
                    if (s->ptt_capture)
                        snprintf(s->error, sizeof(s->error), "%s", reason);
                } else {
                    char reason[192];
                    snprintf(reason, sizeof(reason), "%s", key_binding_error(&captured));
                    begin_capture(s);
                    if (s->ptt_capture)
                        snprintf(s->error, sizeof(s->error), "%s", reason);
                }
            }
        } else if (type == KEY_CODE_YES && key == TEXT_CAPTURE_CANCELLED)
            s->ptt_capture = false;
        else if (!text_capture_active())
            s->ptt_capture = false;
        return;
    }
    if (s->keys_capture) {
        key_binding captured;
        if (type == KEY_CODE_YES && key == TEXT_CAPTURED && text_capture_take(&captured)) {
            s->keys_capture = false;
            keys_captured(s, captured);
        } else if ((type == KEY_CODE_YES && key == TEXT_CAPTURE_CANCELLED) ||
                   !text_capture_active()) {
            s->keys_capture = false;
            s->error[0] = 0;
        }
        return;
    }
    /* Esc first closes the Atalhos menu, a confirmation or a pending key. */
    if (s->section == SETTINGS_SHORTCUTS && s->focus == 1 &&
        (s->keys_menu || s->keys_reset_confirm || s->keys_conflict != KEYMAP_NONE)) {
        keys_key(s, type, key);
        return;
    }
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == OK && key == 27) {
        settings_close(s);
        return;
    }
    if (shortcut_is(type, key, KEYMAP_SETTINGS)) {
        settings_close(s);
        return;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        s->focus = (s->focus + (type == OK ? 1 : 3)) % 4;
        if (!has_content(s->section) && s->focus == 1)
            s->focus = type == OK ? 2 : 0;
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_MOUSE && s->window) {
        MEVENT e;
        if (ui_mouse_get(&e) != OK || !(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
            return;
        int y = e.y, x = e.x;
        if (!wmouse_trafo(s->window, &y, &x, false))
            return;
        if (y == 3) {
            voice_test(s->voice, false);
            s->path_copied = false;
            s->section =
                tab_strip_hit(&s->tabs, SETTINGS_SECTIONS, s->section, x, getmaxx(s->window));
            s->focus = 0;
            s->about_copied = false;
        } else if (s->section == SETTINGS_ACCESSIBILITY && y >= 5 && y < getmaxy(s->window) - 4) {
            access_click(s, y, x);
        } else if (s->section == SETTINGS_SHORTCUTS && y >= 5 && y < getmaxy(s->window) - 3) {
            keys_click(s, y, x);
        } else if (s->section == SETTINGS_PROFILE && y >= 5 && y < getmaxy(s->window) - 3) {
            profile_click(s, y, x);
        } else if (s->section == SETTINGS_ACCOUNT &&
                   (y == s->account_y[0] || y == s->account_y[1])) {
            s->focus = 1;
            s->account_item = y == s->account_y[1];
        } else if (s->section == SETTINGS_AUDIO && voice_available() && y >= 5 &&
                   y < getmaxy(s->window) - 6) {
            int selected = s->audio_first + y - 5;
            if (selected < 9) {
                s->audio_selection = selected;
                s->focus = 1;
                audio_adjust(s, x < getmaxx(s->window) / 2 ? -1 : 1);
            }
        } else if (s->section == SETTINGS_APPEARANCE && y >= 5 &&
                   y < 5 + appearance_rows(getmaxy(s->window))) {
            appearance_click(s, y, x);
        } else if (s->section == SETTINGS_NOTIFICATIONS && s->profile && y >= 7 &&
                   y < 7 + NOTIFY_ITEMS && y < getmaxy(s->window) - 4) {
            s->notify_selection = y - 7;
            s->focus = 1;
            bool *value = notify_value(s, s->notify_selection);
            *value = !*value;
            s->error[0] = 0;
        } else if (s->section == SETTINGS_ABOUT && y >= 5 && y < getmaxy(s->window) - 3) {
            about_action action = about_click(&s->about, y);
            if (action != ABOUT_NOTHING) {
                s->focus = 1;
                about_act(s, config, action);
            }
        } else if (y == getmaxy(s->window) - 2) {
            if (x >= 2 && x < 20)
                save(s, config);
            else if (x >= getmaxx(s->window) - 14)
                settings_close(s);
        }
        return;
    }
    if (s->focus == 0) {
        if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
            voice_test(s->voice, false);
            s->path_copied = false;
            s->section =
                (s->section + (key == KEY_LEFT ? SETTINGS_SECTIONS - 1 : 1)) % SETTINGS_SECTIONS;
            s->about_copied = false;
        } else if (enter || (type == KEY_CODE_YES && key == KEY_DOWN))
            s->focus = has_content(s->section) ? 1 : 2;
    } else if (s->focus == 1) {
        if (s->section == SETTINGS_PROFILE) {
            profile_key(s, type, key);
            return;
        }
        if (s->section == SETTINGS_ACCOUNT) {
            account_key(s, type, key);
            return;
        }
        if (s->section == SETTINGS_ABOUT) {
            about_act(s, config, about_key(&s->about, type, key));
            return;
        }
        if (s->section == SETTINGS_SHORTCUTS) {
            keys_key(s, type, key);
            return;
        }
        if (s->section == SETTINGS_ACCESSIBILITY) {
            access_key(s, type, key);
            return;
        }
        if (s->section == SETTINGS_AUDIO) {
            if (!voice_available()) {
                /* The build has no WebRTC support: the section only reports it. */
                if (type == KEY_CODE_YES && key == KEY_UP)
                    s->focus = 0;
                else if (type == KEY_CODE_YES && key == KEY_DOWN)
                    s->focus = 2;
                return;
            }
            if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
                audio_adjust(s, key == KEY_LEFT ? -1 : 1);
            else if (enter || (type == OK && key == ' '))
                audio_adjust(s, 1);
            else if (type == KEY_CODE_YES && key == KEY_UP) {
                if (!s->audio_selection)
                    s->focus = 0;
                else
                    --s->audio_selection;
            } else if (type == KEY_CODE_YES && key == KEY_DOWN) {
                if (s->audio_selection == 8)
                    s->focus = 2;
                else
                    ++s->audio_selection;
            }
            return;
        }
        if (s->section == SETTINGS_NOTIFICATIONS)
            notifications_key(s, type, key);
        else
            appearance_key(s, type, key);
    } else if (type == KEY_CODE_YES && key == KEY_UP) {
        s->focus = has_content(s->section) ? 1 : 0;
    } else if (enter) {
        if (s->focus == 2)
            save(s, config);
        else
            settings_close(s);
    } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
        s->focus = key == KEY_LEFT ? 2 : 3;
}
