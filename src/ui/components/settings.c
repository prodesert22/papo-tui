#include "ui/components/settings.h"
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
static const char *sections[] = {"Geral", "Aparência", "Áudio", "Acessibilidade", "Atalhos"};
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
void settings_open(app_settings *s, const profile_config *config, voice_client *voice)
{
    settings_close(s);
    memset(s, 0, sizeof(*s));
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
    int best_first = active, best_last = active, best_count = 0, best_balance = 5;
    char label[64];
    for (int first = 0; first <= active; ++first) {
        for (int last = active; last < 5; ++last) {
            int used = first > 0 ? 2 : 0;
            for (int i = first; i <= last; ++i) {
                used += tab_label(i, active, label, sizeof(label));
                if (i > first)
                    used += 2;
            }
            if (last < 4)
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
    for (int i = 0; i < 5; ++i)
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
    if (last < 4) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 3, x + 1, 1, "›");
        s->tab_next = x + 1;
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
    if (!s->section) {
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
    } else if (s->section == 2) {
        audio_draw(s, w, height, width);
    } else {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 6, 3, width - 6, "Em breve. Esta seção ainda não altera configurações.");
    }
    wattrset(w, COLOR_PAIR(THEME_ERROR));
    if (*s->error)
        text_label(w, height - 3, 2, width - 4, s->error);
    button_draw(w, height - 2, 2, 18, "Salvar e aplicar",
                s->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, height - 2, width - 14, 12, "Cancelar",
                s->focus == 3 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
}
static void save(app_settings *s, profile_config *config)
{
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
        if (s->section && s->section != 2 && s->focus == 1)
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
            for (int i = 0; i < 5; ++i)
                if (s->tab_w[i] > 0 && x >= s->tab_x[i] && x < s->tab_x[i] + s->tab_w[i])
                    chosen = i;
            if (chosen >= 0)
                s->section = chosen;
            else if (s->tab_prev >= 0 && x <= s->tab_prev + 1)
                s->section = (s->section + 4) % 5;
            else if (s->tab_next >= 0 && x >= s->tab_next - 1)
                s->section = (s->section + 1) % 5;
            else
                s->section = (s->section + (x < getmaxx(s->window) / 2 ? 4 : 1)) % 5;
            s->focus = 0;
        } else if (s->section == 2 && voice_available() && y >= 5 && y < getmaxy(s->window) - 6) {
            int selected = s->audio_first + y - 5;
            if (selected < 9) {
                s->audio_selection = selected;
                s->focus = 1;
                audio_adjust(s, x < getmaxx(s->window) / 2 ? -1 : 1);
            }
        } else if (!s->section && y >= 5 &&
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
            s->section = (s->section + (key == KEY_LEFT ? 4 : 1)) % 5;
        } else if (enter || (type == KEY_CODE_YES && key == KEY_DOWN))
            s->focus = s->section && s->section != 2 ? 2 : 1;
    } else if (s->focus == 1) {
        if (s->section == 2) {
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
        s->focus = s->section && s->section != 2 ? 0 : 1;
    } else if (enter) {
        if (s->focus == 2)
            save(s, config);
        else
            settings_close(s);
    } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
        s->focus = key == KEY_LEFT ? 2 : 3;
}
