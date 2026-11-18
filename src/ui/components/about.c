#include "ui/components/about.h"
#include "app/update.h"
#include "model/text/translation.h"
#include "platform/clipboard.h"
#include "platform/install.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The same holder and license as the LICENSE file shipped with the program. */
#define ABOUT_LICENSE "MIT · Copyright (c) 2026 Papo TUI contributors"
#define ABOUT_AUTHOR "Papo TUI contributors"

/* Every line the section can show, in display order. */
enum {
    LINE_VERSION,
    LINE_SUBTITLE,
    LINE_SPACER,
    LINE_LICENSE,
    LINE_AUTHOR,
    LINE_SOURCE,
    LINE_NOTICES,
    LINE_NOTICES_PATH,
    LINE_CHECK,
    LINE_BETA,
    LINE_STATUS,
    LINE_RELEASE,
    LINE_SAVED,
    LINE_COMMAND_LABEL,
    LINE_COMMAND,
    ABOUT_LINES
};
/* What a short area gives up first; the version always stays. */
static const int drop_order[] = {LINE_NOTICES_PATH,  LINE_NOTICES, LINE_SPACER,  LINE_SUBTITLE,
                                 LINE_AUTHOR,        LINE_LICENSE, LINE_RELEASE, LINE_SAVED,
                                 LINE_COMMAND_LABEL, LINE_BETA,    LINE_STATUS,  LINE_CHECK,
                                 LINE_COMMAND,       LINE_SOURCE};

/* Packages install the notices under the installation prefix; a build tree
 * has none. */
static void resolve_licenses(about_view *view)
{
    view->resolved = true;
    view->licenses[0] = 0;
    char *prefix = install_prefix();
    if (!prefix)
        return;
    char *licenses = g_build_filename(prefix, "share", "licenses", "papo-tui", NULL);
    if (g_file_test(licenses, G_FILE_TEST_IS_DIR) && strlen(licenses) < sizeof(view->licenses))
        snprintf(view->licenses, sizeof(view->licenses), "%s", licenses);
    g_free(licenses);
    g_free(prefix);
}
/* The page of the newer release; the tag is rebuilt from the validated version. */
static bool release_url(char *url, size_t size)
{
    const char *latest = update_latest();
    int written =
        *latest ? snprintf(url, size, "%s/releases/tag/v%s", ABOUT_REPOSITORY, latest) : 0;
    return written > 0 && (size_t)written < size;
}
static const char *failure_text(void)
{
    switch (update_error()) {
    case UPDATE_FAILURE_TLS:
        return _("O certificado do GitHub não é confiável; nada foi consultado.");
    case UPDATE_FAILURE_LIMITED:
        return _("O GitHub limitou as consultas deste endereço. Tente mais tarde.");
    case UPDATE_FAILURE_RESPONSE:
        return _("O GitHub respondeu algo inesperado. Tente mais tarde.");
    default:
        return _("Não foi possível consultar o GitHub. Verifique a conexão.");
    }
}
static const char *transfer_failure_text(void)
{
    switch (update_download_error()) {
    case UPDATE_TRANSFER_TLS:
        return _("O certificado do GitHub não é confiável; nada foi baixado.");
    case UPDATE_TRANSFER_REFUSED:
        return _("O GitHub não entregou o arquivo. Tente mais tarde.");
    case UPDATE_TRANSFER_MISMATCH:
        return _("O arquivo baixado não confere com o publicado e foi descartado.");
    case UPDATE_TRANSFER_STORAGE:
        return _("Não foi possível gravar o arquivo temporário. Verifique o espaço em disco.");
    case UPDATE_TRANSFER_UNUSABLE:
        return _("O arquivo baixado não contém a versão esperada do programa e foi descartado.");
    case UPDATE_TRANSFER_UNSIGNED:
        return _("Esta versão não publica uma assinatura; nada foi baixado.");
    case UPDATE_TRANSFER_FORGED:
        return _("A assinatura desta versão não confere; nada foi baixado.");
    default:
        return _("Não foi possível baixar a atualização. Verifique a conexão.");
    }
}
/* What the user types, or runs, to put the saved package in place: the label
 * that introduces it and the text itself. false when nothing was saved. */
static bool command_text(const char **label, char *command, size_t size)
{
    const char *saved = update_saved();
    if (!*saved)
        return false;
#ifdef _WIN32
    char *quoted = g_strdup_printf("\"%s\"", saved);
#else
    char *quoted = g_shell_quote(saved);
#endif
    const char *verb = "";
    switch (update_channel()) {
    case INSTALL_DEB_UBUNTU:
    case INSTALL_DEB_DEBIAN:
        *label = _("Instale com:");
        verb = "sudo apt install ";
        break;
    case INSTALL_RPM_FEDORA:
        *label = _("Instale com:");
        verb = "sudo dnf install ";
        break;
    case INSTALL_ARCH:
        *label = _("Instale com:");
        verb = "sudo pacman -U ";
        break;
    case INSTALL_FLATPAK:
        *label = _("Instale com:");
        verb = "flatpak install --user ";
        break;
    case INSTALL_TERMUX:
        *label = _("Instale com:");
        verb = "apt install ";
        break;
    case INSTALL_APPIMAGE:
        *label = _("Torne o arquivo executável e abra-o no lugar do atual:");
        verb = "chmod +x ";
        break;
    case INSTALL_TARBALL:
        *label = _("Feche o programa e extraia sobre a pasta atual:");
        verb = "tar -xzf ";
        break;
    case INSTALL_WINDOWS_INSTALLER:
        *label = _("Feche o programa e execute o instalador:");
        break;
    default:
        *label = _("Feche o programa e extraia o arquivo sobre a pasta atual:");
        break;
    }
    int written = snprintf(command, size, "%s%s", verb, quoted);
    g_free(quoted);
    return written > 0 && (size_t)written < size;
}
/* The state of the download when there is one, otherwise of the check; a known
 * newer version is named unless the last check failed, whose reason matters
 * more. Returns the color pair. */
static int status_text(char *line, size_t size)
{
    update_state state = update_status();
    const char *latest = update_latest();
    update_transfer transfer = update_download_status();
    if (transfer == UPDATE_TRANSFER_AUTHENTICATING) {
        snprintf(line, size, "%s", _("Conferindo a assinatura da versão…"));
        return THEME_PANEL_MUTED;
    }
    if (transfer == UPDATE_TRANSFER_RUNNING) {
        snprintf(line, size, _("Baixando %s… %d%%"), update_asset_name(),
                 update_download_percent());
        return THEME_PANEL_MUTED;
    }
    if (update_install_staged()) {
        snprintf(line, size, _("Versão %s pronta: Instalar e sair a coloca no lugar desta."),
                 latest);
        return THEME_SUCCESS;
    }
    if (transfer == UPDATE_TRANSFER_VERIFYING || transfer == UPDATE_TRANSFER_UNPACKING ||
        transfer == UPDATE_TRANSFER_READY) {
        snprintf(line, size, "%s",
                 transfer == UPDATE_TRANSFER_VERIFYING
                     ? _("Conferindo o tamanho e o SHA-256 do arquivo…")
                 : transfer == UPDATE_TRANSFER_UNPACKING ? _("Preparando a versão nova…")
                                                         : _("Arquivo conferido. Salvando…"));
        return THEME_PANEL_MUTED;
    }
    if (transfer == UPDATE_TRANSFER_FAILED) {
        snprintf(line, size, "%s", transfer_failure_text());
        return THEME_ERROR;
    }
    if (state == UPDATE_CHECKING) {
        snprintf(line, size, "%s", _("Verificando atualizações…"));
        return THEME_PANEL_MUTED;
    }
    if (state == UPDATE_FAILED) {
        snprintf(line, size, "%s", failure_text());
        return THEME_ERROR;
    }
    if (*latest) {
        /* The release page is always a way to get it by hand; the button is
         * another when this installation has a package there. */
        snprintf(line, size,
                 *update_asset_name()
                     ? _("Versão %s disponível. Baixe com o botão ou manualmente em:")
                     : _("Versão %s disponível. Baixe manualmente em:"),
                 latest);
        return THEME_PANEL_ACCENT;
    }
    snprintf(line, size, "%s",
             state == UPDATE_CURRENT ? _("Você está na versão mais recente.")
             : update_suppressed()
                 ? _("PAPO_UPDATE_CHECK=0: as atualizações só são verificadas a pedido.")
             : update_development()
                 ? _("Versão de desenvolvimento: as atualizações só são verificadas a pedido.")
                 : _("Atualizações ainda não verificadas."));
    return THEME_PANEL_MUTED;
}
static void item_attributes(const about_view *view, WINDOW *window, about_item item, bool focused)
{
    bool selected = focused && view->selection == (int)item;
    wattrset(window, COLOR_PAIR(selected ? THEME_FOCUSED : THEME_PANEL) |
                         (selected && !has_colors() ? A_REVERSE : 0));
}

void about_draw(about_view *view, WINDOW *window, int top, int left, int width, int rows,
                bool focused, bool automatic, bool prereleases)
{
    for (int i = 0; i < ABOUT_ITEMS; ++i)
        view->rows[i] = -1;
    if (rows < 1 || width < 1)
        return;
    if (!view->resolved)
        resolve_licenses(view);
    char line[1280], release[256], command[1200];
    const char *command_label = "";
    bool shown[ABOUT_LINES];
    int count = ABOUT_LINES;
    for (int i = 0; i < ABOUT_LINES; ++i)
        shown[i] = true;
    if (!*view->licenses) {
        shown[LINE_NOTICES_PATH] = false;
        --count;
    }
    if (!release_url(release, sizeof(release))) {
        shown[LINE_RELEASE] = false;
        --count;
    }
    if (!command_text(&command_label, command, sizeof(command))) {
        shown[LINE_SAVED] = shown[LINE_COMMAND_LABEL] = shown[LINE_COMMAND] = false;
        count -= 3;
    }
    for (size_t i = 0; count > rows && i < sizeof(drop_order) / sizeof(drop_order[0]); ++i)
        if (shown[drop_order[i]]) {
            shown[drop_order[i]] = false;
            --count;
            /* The folder's label says nothing without the folder. */
            if (drop_order[i] == LINE_NOTICES_PATH) {
                shown[LINE_NOTICES] = false;
                --count;
            }
        }
    int row = top;
    for (int i = 0; i < ABOUT_LINES; ++i) {
        if (!shown[i])
            continue;
        switch (i) {
        case LINE_VERSION:
            wattrset(window, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
            snprintf(line, sizeof(line), "Papo TUI %s", PAPO_VERSION);
            text_label(window, row, left, width, line);
            break;
        case LINE_SUBTITLE:
            wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(window, row, left, width, _("Cliente de conversas para terminal"));
            break;
        case LINE_LICENSE:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            snprintf(line, sizeof(line), _("Licença: %s"), ABOUT_LICENSE);
            text_label(window, row, left, width, line);
            break;
        case LINE_AUTHOR:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            snprintf(line, sizeof(line), _("Autor: %s"), ABOUT_AUTHOR);
            text_label(window, row, left, width, line);
            break;
        case LINE_SOURCE:
            item_attributes(view, window, ABOUT_SOURCE, focused);
            snprintf(line, sizeof(line), _("Código-fonte: %s"), ABOUT_REPOSITORY);
            text_label(window, row, left, width, line);
            view->rows[ABOUT_SOURCE] = row;
            break;
        case LINE_NOTICES:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            text_label(window, row, left, width,
                       *view->licenses
                           ? _("Licenças de terceiros:")
                           : _("Licenças de terceiros: THIRD_PARTY_NOTICES.md no repositório"));
            break;
        case LINE_NOTICES_PATH:
            wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(window, row, left + 2, width - 2, view->licenses);
            break;
        case LINE_CHECK:
            item_attributes(view, window, ABOUT_CHECK, focused);
            snprintf(line, sizeof(line), _("[%c] Verificar atualizações automaticamente"),
                     automatic ? 'x' : ' ');
            text_label(window, row, left, width, line);
            view->rows[ABOUT_CHECK] = row;
            break;
        case LINE_BETA:
            item_attributes(view, window, ABOUT_BETA, focused);
            snprintf(line, sizeof(line), _("[%c] Aceitar versões beta"), prereleases ? 'x' : ' ');
            text_label(window, row, left, width, line);
            view->rows[ABOUT_BETA] = row;
            break;
        case LINE_STATUS:
            wattrset(window, COLOR_PAIR(status_text(line, sizeof(line))));
            text_label(window, row, left, width, line);
            break;
        case LINE_RELEASE:
            item_attributes(view, window, ABOUT_RELEASE, focused);
            text_label(window, row, left, width, release);
            view->rows[ABOUT_RELEASE] = row;
            break;
        case LINE_SAVED:
            wattrset(window, COLOR_PAIR(THEME_SUCCESS));
            snprintf(line, sizeof(line), _("Pacote conferido e salvo em %s"), update_saved());
            text_label(window, row, left, width, line);
            break;
        case LINE_COMMAND_LABEL:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            text_label(window, row, left, width, command_label);
            break;
        case LINE_COMMAND:
            item_attributes(view, window, ABOUT_COMMAND, focused);
            text_label(window, row, left, width, command);
            view->rows[ABOUT_COMMAND] = row;
            break;
        default:
            break;
        }
        ++row;
    }
    /* The selection follows the lines that exist now. */
    if (view->selection < 0 || view->selection >= ABOUT_ITEMS || view->rows[view->selection] < 0)
        for (int i = 0; i < ABOUT_ITEMS; ++i)
            if (view->rows[i] >= 0) {
                view->selection = i;
                break;
            }
}
static about_action action_of(int item)
{
    return item == ABOUT_SOURCE    ? ABOUT_COPY_SOURCE
           : item == ABOUT_CHECK   ? ABOUT_TOGGLE_CHECK
           : item == ABOUT_BETA    ? ABOUT_TOGGLE_BETA
           : item == ABOUT_RELEASE ? ABOUT_COPY_RELEASE
                                   : ABOUT_COPY_COMMAND;
}
/* The next shown item in a direction, or -1 at the edge. */
static int neighbor(const about_view *view, int direction)
{
    for (int i = view->selection + direction; i >= 0 && i < ABOUT_ITEMS; i += direction)
        if (view->rows[i] >= 0)
            return i;
    return -1;
}
about_action about_key(about_view *view, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        int next = neighbor(view, key == KEY_UP ? -1 : 1);
        if (next < 0)
            return key == KEY_UP ? ABOUT_LEAVE_UP : ABOUT_LEAVE_DOWN;
        view->selection = next;
        return ABOUT_NOTHING;
    }
    if ((enter || (type == OK && key == ' ')) && view->selection >= 0 &&
        view->selection < ABOUT_ITEMS && view->rows[view->selection] >= 0)
        return action_of(view->selection);
    return ABOUT_NOTHING;
}
about_action about_click(about_view *view, int row)
{
    for (int i = 0; i < ABOUT_ITEMS; ++i)
        if (view->rows[i] >= 0 && view->rows[i] == row) {
            view->selection = i;
            return action_of(i);
        }
    return ABOUT_NOTHING;
}
bool about_copy_source(void)
{
    return clipboard_copy_text(ABOUT_REPOSITORY);
}
bool about_copy_release(void)
{
    char url[256];
    return release_url(url, sizeof(url)) && clipboard_copy_text(url);
}
bool about_copy_command(void)
{
    char command[1200];
    const char *label;
    return command_text(&label, command, sizeof(command)) && clipboard_copy_text(command);
}
about_button about_button_now(void)
{
    update_transfer transfer = update_download_status();
    if (transfer == UPDATE_TRANSFER_AUTHENTICATING || transfer == UPDATE_TRANSFER_RUNNING ||
        transfer == UPDATE_TRANSFER_VERIFYING || transfer == UPDATE_TRANSFER_UNPACKING)
        return ABOUT_BUTTON_CANCEL;
    if (update_install_staged())
        return ABOUT_BUTTON_APPLY;
    if (*update_saved())
        return ABOUT_BUTTON_COPY;
    /* While a check runs its result decides what the button offers. */
    if (!*update_asset_name() || update_status() == UPDATE_CHECKING)
        return ABOUT_BUTTON_CHECK;
    return update_install_possible() ? ABOUT_BUTTON_INSTALL : ABOUT_BUTTON_DOWNLOAD;
}
