#include <curl/curl.h>
#include <glib.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "model/text/mentions.h"
#include "model/text/translation.h"
#include "platform/file_transfer.h"
#include "platform/terminal.h"
#include "ui/media/resources.h"
#include "ui/screens/login.h"

static void put_span(const char *text, size_t length, const char *style)
{
    if (style)
        fputs(style, stdout);
    fwrite(text, 1, length, stdout);
    if (style)
        fputs("\033[0m", stdout);
}
/* Text after the term of a line, with links underlined. */
static void put_prose(const char *text, size_t length)
{
    while (length) {
        const char *link = g_strstr_len(text, (gssize)length, "https://");
        size_t before = link ? (size_t)(link - text) : length;
        put_span(text, before, NULL);
        text += before;
        length -= before;
        if (!link)
            break;
        size_t span = 0;
        while (span < length && text[span] != ' ')
            ++span;
        put_span(text, span, "\033[4;34m");
        text += span;
        length -= span;
    }
}
/* Colors only mark the structure: the title, the headings, the commands, the
 * term of each list entry and links. The translated text stays unchanged. */
static void put_help(const char *text, bool color)
{
    if (!color) {
        fputs(text, stdout);
        return;
    }
    for (bool first = true; *text; first = false) {
        const char *newline = strchr(text, '\n');
        size_t length = newline ? (size_t)(newline - text) : strlen(text);
        const char *gap = length > 2 ? g_strstr_len(text + 2, (gssize)length - 2, "  ") : NULL;
        if (first)
            put_span(text, length, "\033[1m");
        else if (length && text[0] != ' ' && text[length - 1] == ':')
            put_span(text, length, "\033[1;33m");
        else if (length > 11 && !strncmp(text, "  papo-tui ", 11)) {
            put_span(text, 2, NULL);
            put_span(text + 2, length - 2, "\033[36m");
        } else if (length > 2 && !strncmp(text, "  ", 2) && text[2] != ' ' && gap) {
            put_span(text, 2, NULL);
            put_span(text + 2, (size_t)(gap - text) - 2, "\033[32m");
            put_prose(gap, length - (size_t)(gap - text));
        } else
            put_prose(text, length);
        if (newline)
            putchar('\n');
        text += length + (newline ? 1 : 0);
    }
}
/* Command line reference, at most 80 columns wide. Keyboard controls live in
 * the in-app help and the usage guide, not here. */
static void print_help(void)
{
    GString *help = g_string_new(NULL);
    g_string_append_printf(help,
                           _("papo-tui %s\n"
                             "Cliente de conversas para o terminal: canais, mensagens diretas, "
                             "menções,\n"
                             "mídia e chamadas de voz em servidores Papo.\n"),
                           PAPO_VERSION);
    g_string_append(help, _("\nUso:\n"
                            "  papo-tui [--profiles] [--allow-local-http]\n"
                            "  papo-tui --help | --version\n"
                            "\n"
                            "Sem opções, reabre os servidores com sessão salva, cada um na última\n"
                            "conversa aberta. Sem sessão salva, abre a tela de entrada, onde você\n"
                            "informa o endereço do servidor, o usuário e a senha.\n"));
    g_string_append(help,
                    _("\nOpções:\n"
                      "  --profiles          Abre primeiro o gerenciador de servidores salvos.\n"
                      "  --allow-local-http  Aceita HTTP sem TLS somente em localhost, 127.0.0.1\n"
                      "                      e [::1], para servidores de desenvolvimento. Os\n"
                      "                      demais endereços continuam exigindo HTTPS.\n"
                      "  --help              Mostra esta ajuda e sai.\n"
                      "  --version           Mostra a versão e sai.\n"));
    g_string_append(help,
                    _("\nVariáveis de ambiente:\n"
                      "  PAPO_LANGUAGE=pt|en   Idioma da interface quando a opção Idioma está em\n"
                      "                        Sistema. Sem ela, segue o idioma do sistema.\n"
                      "  CURL_CA_BUNDLE=ARQ    Arquivo PEM com a autoridade certificadora de um\n"
                      "                        servidor com certificado próprio. A verificação\n"
                      "                        TLS continua ativa.\n"
                      "  NO_COLOR=1            Mostra esta ajuda sem cores. Fora de um terminal\n"
                      "                        ela já sai sem cores.\n"));
#ifdef _WIN32
    g_string_append(help,
                    _("\nArquivos:\n"
                      "  %LOCALAPPDATA%\\papo-tui         Perfis e preferências.\n"
                      "  %LOCALAPPDATA%\\papo-tui\\state   Sessões, leituras e rascunhos.\n"));
#else
    g_string_append(
        help, _("\nArquivos:\n"
                "  ~/.config/papo-tui       Perfis e preferências ($XDG_CONFIG_HOME).\n"
                "  ~/.local/state/papo-tui  Sessões, leituras e rascunhos ($XDG_STATE_HOME).\n"));
#endif
    g_string_append(
        help, _("  Senhas não são gravadas. Os cookies de sessão ficam no cofre de\n"
                "  credenciais do sistema.\n"
                "\n"
                "Guia de uso: https://github.com/prodesert22/papo-tui/blob/main/docs/usage.md\n"));
    bool color = terminal_color_begin();
    put_help(help->str, color);
    fflush(stdout);
    if (color)
        terminal_color_end();
    g_string_free(help, true);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("papo-tui " PAPO_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }
    bool profiles = false, allow_http = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--profiles") == 0 && !profiles)
            profiles = true;
        else if (strcmp(argv[i], "--allow-local-http") == 0 && !allow_http)
            allow_http = true;
        else {
            fputs(_("Argumento desconhecido ou repetido. Use papo-tui --help.\n"), stderr);
            return 1;
        }
    }
#ifdef _WIN32
    const char *locale = ".UTF8";
#else
    const char *locale = "";
#endif
    /* libmpv requires C numeric formatting; preserve the terminal's UTF-8 locale. */
    if (!setlocale(LC_ALL, locale) || !setlocale(LC_NUMERIC, "C")) {
        fputs(_("Não foi possível inicializar a localidade do terminal.\n"), stderr);
        return 1;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fputs(_("Não foi possível inicializar a comunicação.\n"), stderr);
        return 1;
    }
    media_resources_init();
    int result = login_run_profiles(profiles, allow_http);
    file_transfer_shutdown();
    mention_cleanup();
    curl_global_cleanup();
    return result;
}
