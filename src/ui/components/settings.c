#include "ui/components/settings.h"
#include "model/conversation/profile_edit.h"
#include "ui/components/button.h"
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
static const char *sections[SETTINGS_SECTIONS] = {"Perfil", "Aparência", "Áudio", "Acessibilidade",
                                                  "Atalhos"};
/* Sections that have controls between the tabs and the buttons. */
static bool has_content(int section)
{
    return section == SETTINGS_APPEARANCE || section == SETTINGS_PROFILE ||
           section == SETTINGS_AUDIO;
}
void settings_close(app_settings *s)
{
    if (s->ptt_capture) {
        text_capture_cancel();
        s->ptt_capture = false;
    }
    if (s->open) {
        voice_test(s->voice, false);
        if (!s->applied)
            voice_configure(&s->original_audio);
    }
    s->open = false;
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
}
void settings_open(app_settings *s, const profile_config *config, voice_client *voice,
                   history *profile, const char *server)
{
    settings_close(s);
    memset(s, 0, sizeof(*s));
    s->profile = profile;
    snprintf(s->server, sizeof(s->server), "%s", server ? server : "");
    s->open = true;
    s->draft = config->mode;
    s->selection = config->mode;
    s->audio = s->original_audio = config->audio;
    s->voice = voice;
    s->tab_prev = s->tab_next = -1;
    voice_refresh_devices(voice);
    voice_ptt(false);
}
static const char *device_name(app_settings *s, const char *id, bool input)
{
    if (!*id)
        return "Padrão do sistema";
    size_t i;
    json_t *device;
    json_array_foreach(voice_devices(s->voice), i, device)
    {
        const char *key = json_string_value(json_object_get(device, "id"));
        if (key && !strcmp(id, key) && json_is_true(json_object_get(device, "input")) == input)
            return json_string_value(json_object_get(device, "name"));
    }
    return "Dispositivo indisponível";
}
static void begin_capture(app_settings *s)
{
    if (!voice_available() || s->ptt_capture)
        return;
    voice_test(s->voice, false);
    if (!text_capture_begin()) {
        snprintf(s->error, sizeof(s->error), "Não foi possível capturar a tecla.");
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
            snprintf(label, sizeof(label), "Entrada: < %s >",
                     voice ? device_name(s, s->audio.input, true) : "Indisponível");
            break;
        case 1:
            snprintf(label, sizeof(label), "Saída: < %s >",
                     voice ? device_name(s, s->audio.output, false) : "Indisponível");
            break;
        case 2:
            snprintf(label, sizeof(label), "Volume do microfone: < %d%% >",
                     s->audio.microphone_volume);
            break;
        case 3:
            snprintf(label, sizeof(label), "Volume da chamada: < %d%% >", s->audio.output_volume);
            break;
        case 4:
            snprintf(label, sizeof(label), "[%s teste do microfone]",
                     voice_testing() ? "Parar" : "Iniciar");
            break;
        case 5:
            snprintf(label, sizeof(label), "Pressionar para falar: < %s >",
                     s->audio.push_to_talk ? "Ativado" : "Desativado");
            break;
        case 6: {
            char key[64] = "?";
            if (!s->ptt_capture)
                key_binding_label(&s->audio.ptt, key, sizeof(key));
            snprintf(label, sizeof(label), "Tecla para falar: < %s >",
                     s->ptt_capture ? "pressione uma combinação" : key);
            break;
        }
        case 7:
            snprintf(label, sizeof(label), "Fluxos desta instância: < %u >",
                     audio_config_slots(&s->audio, voice_instance(s->voice)));
            break;
        default:
            snprintf(label, sizeof(label), "[Atualizar dispositivos]");
            break;
        }
        bool focused = s->focus == 1 && s->audio_selection == item;
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                        (focused && !has_colors() ? A_REVERSE : 0));
        text_label(w, row + 5, 2, width - 4, label);
    }
    double rms = voice_testing() ? voice_level(s->voice, false) : -120;
    double peak = voice_testing() ? voice_level(s->voice, true) : -120;
    int length = width - 18;
    int filled = rms <= -60 ? 0 : rms >= 0 ? length : (int)((rms + 60) * length / 60);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 6, 2, 7, "Nível:");
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
    text_label(w, height - 5, 2, width - 4,
               !voice ? "Voz indisponível: esta compilação não inclui suporte WebRTC."
               : s->ptt_capture
                   ? "Pressione a combinação completa; Ctrl/Alt sozinhos não contam. Esc cancela. "
                     "Mouse 4/5."
               : voice_testing() ? "Retorno local ativo; sua voz não é enviada à chamada."
               : s->focus == 1 && s->audio_selection == 6
                   ? "Enter captura qualquer combinação ou os botões 4/5 do mouse."
                   : "←/→ ajusta; Enter alterna. O teste reproduz sua voz localmente.");
    text_label(w, height - 4, 2, width - 4,
               !voice ? "As opções de áudio ficam disponíveis quando o WebRTC estiver presente."
               : s->audio_selection == 7
                   ? "Use o valor VOICE_AUDIO_SLOTS do servidor; vale no próximo ingresso."
               : s->audio.push_to_talk && s->audio.ptt.kind != KEY_BINDING_MOUSE &&
                       !text_keyboard_events_supported()
                   ? key_binding_legacy_ctrl_byte(&s->audio.ptt)
                         ? "Terminal sem soltura: a tecla alterna o microfone."
                         : "Tecla sem eventos neste terminal: use o botão Segure para falar."
                   : "O microfone e a saída podem ser silenciados ao lado de ⚙.");
}
static int tab_label(int section, int active, char *label, size_t size)
{
    if (section == active)
        snprintf(label, size, "< %s >", sections[section]);
    else
        snprintf(label, size, "%s", sections[section]);
    return text_label(NULL, 0, 0, (int)size, label);
}
static void tab_window(int active, int available, int *first, int *last)
{
    int best_first = active, best_last = active, best_count = 0, best_balance = SETTINGS_SECTIONS;
    char label[64];
    for (int first = 0; first <= active; ++first) {
        for (int last = active; last < SETTINGS_SECTIONS; ++last) {
            int used = first > 0 ? 2 : 0;
            for (int i = first; i <= last; ++i) {
                used += tab_label(i, active, label, sizeof(label));
                if (i > first)
                    used += 2;
            }
            if (last < SETTINGS_SECTIONS - 1)
                used += 2;
            if (used > available)
                continue;
            int count = last - first + 1;
            int balance = (active - first) - (last - active);
            if (balance < 0)
                balance = -balance;
            if (count > best_count || (count == best_count && balance < best_balance)) {
                best_first = first;
                best_last = last;
                best_count = count;
                best_balance = balance;
            }
        }
    }
    *first = best_first;
    *last = best_last;
}
static void tabs_draw(app_settings *s, WINDOW *w, int width)
{
    for (int i = 0; i < SETTINGS_SECTIONS; ++i)
        s->tab_w[i] = 0;
    s->tab_prev = s->tab_next = -1;
    int first, last, x = 2;
    char label[64];
    tab_window(s->section, width - 4, &first, &last);
    if (first > 0) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 3, x, 1, "‹");
        s->tab_prev = x;
        x += 2;
    }
    for (int i = first; i <= last; ++i) {
        if (i > first)
            x += 2;
        int label_width = tab_label(i, s->section, label, sizeof(label));
        s->tab_x[i] = x;
        s->tab_w[i] = label_width;
        wattrset(w,
                 COLOR_PAIR(i == s->section ? (s->focus == 0 ? THEME_FOCUSED : THEME_PANEL_ACCENT)
                                            : THEME_PANEL) |
                     (i == s->section ? A_BOLD : 0) |
                     (i == s->section && !has_colors() && s->focus == 0 ? A_REVERSE : 0));
        text_label(w, 3, x, label_width, label);
        x += label_width;
    }
    if (last < SETTINGS_SECTIONS - 1) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 3, x + 1, 1, "›");
        s->tab_next = x + 1;
    }
}
/* Perfil: items 0-3 are the text fields, then status, avatar and banner. */
#define PROFILE_ITEMS 7
enum { ITEM_STATUS = PROFILE_FIELDS, ITEM_AVATAR, ITEM_BANNER };
static const char *const field_labels[PROFILE_FIELDS] = {"Apelido", "Frase de status", "Descrição",
                                                         "Frase de digitação"};
static const char *const field_keys[PROFILE_FIELDS] = {"nickname", "status", "description",
                                                       "typing"};
/* The status phrase is read back from the profile as status_message. */
static const char *const profile_keys[PROFILE_FIELDS] = {"nickname", "status_message",
                                                         "description", "typing"};
static const size_t field_max[PROFILE_FIELDS] = {PROFILE_NICKNAME_MAX, PROFILE_STATUS_MAX,
                                                 PROFILE_DESCRIPTION_MAX, PROFILE_TYPING_MAX};
static const char *const status_labels[3] = {"Online", "Ausente", "Ocupado"};
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
    return changed;
}
static void profile_notice_draw(app_settings *s, WINDOW *w, int height, int width)
{
    profile_state state = history_profile_state(s->profile);
    const char *notice = history_profile_notice(s->profile);
    if (state == PROFILE_SAVING && !*notice)
        notice = "Salvando…";
    if (!*notice)
        return;
    wattrset(w, COLOR_PAIR(state == PROFILE_FAILED || state == PROFILE_UNCERTAIN ? THEME_ERROR
                           : state == PROFILE_SAVED ? THEME_SUCCESS
                                                    : THEME_PANEL_MUTED));
    text_label(w, height - 3, 2, width - 4, notice);
}
static void profile_draw(app_settings *s, WINDOW *w, int height, int width)
{
    s->cursor = false;
    for (int i = 0; i < PROFILE_ITEMS; ++i)
        s->item_y[i] = s->item_rows[i] = -1;
    char line[320];
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    snprintf(line, sizeof(line), "Seu perfil em %s", *s->server ? s->server : "este servidor");
    text_label(w, 5, 2, width - 4, line);
    const json_t *profile = history_profile(s->profile);
    if (!profile) {
        text_label(w, 7, 3, width - 6,
                   history_profile_state(s->profile) == PROFILE_FAILED
                       ? "Não foi possível carregar o perfil. Enter tenta de novo."
                       : "Carregando perfil…");
        return;
    }
    int top = 6, bottom = height - 3, available = bottom - top;
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
        bool focused = s->focus == 1 && s->profile_item == i;
        wattrset(w,
                 COLOR_PAIR(focused ? THEME_PANEL_ACCENT : THEME_PANEL) | (focused ? A_BOLD : 0));
        snprintf(line, sizeof(line), "%s:",
                 i < PROFILE_FIELDS ? field_labels[i]
                 : i == ITEM_STATUS ? "Status"
                 : i == ITEM_AVATAR ? "Avatar"
                                    : "Banner");
        text_label(w, y, 2, label_width - 1, line);
        if (i < PROFILE_FIELDS) {
            text_buffer *field = &s->fields[i];
            wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_INPUT) |
                            (focused && !has_colors() ? A_REVERSE : 0));
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
            snprintf(line, sizeof(line), "< %s >", status_labels[s->status_choice]);
            button_draw(w, y, fx, 13, line, focused ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        } else if (i == ITEM_AVATAR) {
            button_draw(w, y, fx, 12, "Alterar…", focused ? BUTTON_FOCUSED : BUTTON_NORMAL,
                        BUTTON_PANEL);
        } else {
            bool has_banner = *profile_text(profile, "banner_media");
            button_draw(w, y, fx, 12, "Alterar…",
                        focused && !s->banner_button ? BUTTON_FOCUSED : BUTTON_NORMAL,
                        BUTTON_PANEL);
            if (has_banner)
                button_draw(w, y, fx + 14, 11, "Remover",
                            focused && s->banner_button ? BUTTON_FOCUSED : BUTTON_NORMAL,
                            BUTTON_PANEL);
            else {
                s->banner_button = 0;
                wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
                text_label(w, y, fx + 14, width - fx - 16, "sem banner");
            }
        }
        y += rows[i];
    }
}
bool settings_cursor(const app_settings *s)
{
    if (!s->open || s->section != SETTINGS_PROFILE || !s->cursor || !s->window)
        return false;
    setsyx(s->cursor_y, s->cursor_x);
    return true;
}
bool settings_paste(app_settings *s, text_paste *paste)
{
    if (!s->open || s->section != SETTINGS_PROFILE || s->focus != 1 ||
        s->profile_item >= PROFILE_FIELDS || !history_profile(s->profile))
        return false;
    if (!text_paste_commit(paste, &s->fields[s->profile_item]))
        snprintf(s->error, sizeof(s->error), "Colagem excede o limite do campo.");
    else
        s->error[0] = 0;
    s->dirty = true;
    return true;
}
static void profile_save(app_settings *s)
{
    const json_t *profile = history_profile(s->profile);
    if (!profile) {
        snprintf(s->error, sizeof(s->error), "O perfil ainda não foi carregado.");
        return;
    }
    static char text[PROFILE_FIELDS][TEXT_UTF8_BYTES];
    bool changed[PROFILE_FIELDS], any = false;
    for (int i = 0; i < PROFILE_FIELDS; ++i) {
        const char *problem = text_encode(&s->fields[i], text[i])
                                  ? history_profile_check(field_keys[i], text[i])
                                  : "Texto inválido.";
        if (problem) {
            snprintf(s->error, sizeof(s->error), "%s", problem);
            s->profile_item = i;
            s->focus = 1;
            return;
        }
        changed[i] = strcmp(text[i], profile_text(profile, profile_keys[i])) != 0;
        any |= changed[i];
    }
    bool status = s->status_choice != s->loaded_status;
    if (!any && !status) {
        snprintf(s->error, sizeof(s->error), "Nenhuma alteração para salvar.");
        return;
    }
    s->error[0] = 0;
    if (any &&
        !history_profile_save(s->profile, changed[0] ? text[0] : NULL, changed[1] ? text[1] : NULL,
                              changed[2] ? text[2] : NULL, changed[3] ? text[3] : NULL)) {
        snprintf(s->error, sizeof(s->error), "Não foi possível preparar a alteração do perfil.");
        return;
    }
    if (status && !history_profile_set_status(s->profile, status_values[s->status_choice])) {
        snprintf(s->error, sizeof(s->error), "Não foi possível preparar a mudança de status.");
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
            snprintf(s->error, sizeof(s->error), "Não foi possível remover o banner agora.");
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
void settings_draw(app_settings *s)
{
    if (!s->open)
        return;
    int width = COLS > 76 ? 76 : COLS - 2, height = LINES > 20 ? 20 : LINES - 2;
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
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, "Configurações");
    tabs_draw(s, w, width);
    char label[160];
    if (s->section == SETTINGS_APPEARANCE) {
        int first = first_mode(s);
        for (int i = first; i < 3; ++i) {
            int step = height >= 18 ? 3 : 1;
            int row = 5 + (i - first) * step;
            wattrset(w,
                     COLOR_PAIR(s->focus == 1 && s->selection == i ? THEME_FOCUSED : THEME_PANEL) |
                         (!has_colors() && s->focus == 1 && s->selection == i ? A_REVERSE : 0));
            snprintf(label, sizeof(label), "(%c) %s", s->draft == (app_mode)i ? '*' : ' ',
                     i == 0   ? "Normal"
                     : i == 1 ? "Light"
                              : "Ultralight");
            text_label(w, row, 2, width - 4, label);
            wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
            if (height >= 18) {
                text_buffer description;
                text_layout measured;
                if (text_decode(&description, app_mode_description((app_mode)i))) {
                    text_measure(&description, width - 6, &measured);
                    text_draw(w, &description, &measured, 0, 2, row + 1, 3);
                }
            }
        }
        if (height < 18) {
            text_buffer description;
            text_layout measured;
            if (text_decode(&description, app_mode_description((app_mode)s->selection))) {
                text_measure(&description, width - 6, &measured);
                text_draw(w, &description, &measured, 0, height - 10, 8, 3);
            }
        }
    } else if (s->section == SETTINGS_AUDIO) {
        audio_draw(s, w, height, width);
    } else if (s->section == SETTINGS_PROFILE) {
        profile_draw(s, w, height, width);
    } else {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 6, 3, width - 6, "Em breve. Esta seção ainda não altera configurações.");
    }
    wattrset(w, COLOR_PAIR(THEME_ERROR));
    if (*s->error)
        text_label(w, height - 3, 2, width - 4, s->error);
    else if (s->section == SETTINGS_PROFILE)
        profile_notice_draw(s, w, height, width);
    bool profile = s->section == SETTINGS_PROFILE;
    button_draw(w, height - 2, 2, 18, profile ? "Salvar perfil" : "Salvar e aplicar",
                s->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, height - 2, width - 14, 12, profile ? "Fechar" : "Cancelar",
                s->focus == 3 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
}
static void save(app_settings *s, profile_config *config)
{
    if (s->section == SETTINGS_PROFILE) {
        profile_save(s);
        return;
    }
    if (profiles_set_settings(config, s->draft, &s->audio)) {
        s->applied = true;
        voice_configure(&s->audio);
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
            snprintf(s->error, sizeof(s->error), "Não foi possível iniciar o teste de áudio.");
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
                         "Limite de configurações de instâncias atingido.");
    } else {
        voice_refresh_devices(s->voice);
        return;
    }
    voice_configure(&s->audio);
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
                if (key_binding_valid(&captured)) {
                    s->audio.ptt = captured;
                    s->error[0] = 0;
                    voice_configure(&s->audio);
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
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == OK && key == 27) {
        settings_close(s);
        return;
    }
    if (shortcut_is(type, key, SHORTCUT_SETTINGS)) {
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
            int chosen = -1;
            for (int i = 0; i < SETTINGS_SECTIONS; ++i)
                if (s->tab_w[i] > 0 && x >= s->tab_x[i] && x < s->tab_x[i] + s->tab_w[i])
                    chosen = i;
            if (chosen >= 0)
                s->section = chosen;
            else if (s->tab_prev >= 0 && x <= s->tab_prev + 1)
                s->section = (s->section + SETTINGS_SECTIONS - 1) % SETTINGS_SECTIONS;
            else if (s->tab_next >= 0 && x >= s->tab_next - 1)
                s->section = (s->section + 1) % SETTINGS_SECTIONS;
            else
                s->section =
                    (s->section + (x < getmaxx(s->window) / 2 ? SETTINGS_SECTIONS - 1 : 1)) %
                    SETTINGS_SECTIONS;
            s->focus = 0;
        } else if (s->section == SETTINGS_PROFILE && y >= 5 && y < getmaxy(s->window) - 3) {
            profile_click(s, y, x);
        } else if (s->section == SETTINGS_AUDIO && voice_available() && y >= 5 &&
                   y < getmaxy(s->window) - 6) {
            int selected = s->audio_first + y - 5;
            if (selected < 9) {
                s->audio_selection = selected;
                s->focus = 1;
                audio_adjust(s, x < getmaxx(s->window) / 2 ? -1 : 1);
            }
        } else if (s->section == SETTINGS_APPEARANCE && y >= 5 &&
                   y < 5 + (3 - first_mode(s)) * (getmaxy(s->window) >= 18 ? 3 : 1)) {
            s->selection = first_mode(s) + (y - 5) / (getmaxy(s->window) >= 18 ? 3 : 1);
            if (s->selection > 2)
                s->selection = 2;
            s->draft = (app_mode)s->selection;
            s->focus = 1;
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
            s->section =
                (s->section + (key == KEY_LEFT ? SETTINGS_SECTIONS - 1 : 1)) % SETTINGS_SECTIONS;
        } else if (enter || (type == KEY_CODE_YES && key == KEY_DOWN))
            s->focus = has_content(s->section) ? 1 : 2;
    } else if (s->focus == 1) {
        if (s->section == SETTINGS_PROFILE) {
            profile_key(s, type, key);
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
        if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
            if (key == KEY_UP && s->selection <= first_mode(s))
                s->focus = 0;
            else {
                s->selection += (key == KEY_UP ? -1 : 1);
                if (s->selection > 2) {
                    s->selection = 2;
                    s->focus = 2;
                }
            }
        } else if (enter || (type == OK && key == ' '))
            s->draft = (app_mode)s->selection;
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
