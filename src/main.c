#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "model/text/mentions.h"
#include "model/text/translation.h"
#include "platform/file_transfer.h"
#include "ui/media/resources.h"
#include "ui/screens/login.h"

/* Command line reference, at most 80 columns wide. Keyboard controls live in
 * the in-app help and the usage guide, not here. */
static void print_help(void)
{
    printf(_("papo-tui %s\n"
             "Cliente de conversas para o terminal: canais, mensagens diretas, menções,\n"
             "mídia e chamadas de voz em servidores Papo.\n"),
           PAPO_VERSION);
    fputs(_("\nUso:\n"
            "  papo-tui [--profiles] [--allow-local-http]\n"
            "  papo-tui --help | --version\n"
            "\n"
            "Sem opções, reabre os servidores com sessão salva, cada um na última\n"
            "conversa aberta. Sem sessão salva, abre a tela de entrada, onde você\n"
            "informa o endereço do servidor, o usuário e a senha.\n"),
          stdout);
    fputs(_("\nOpções:\n"
            "  --profiles          Abre primeiro o gerenciador de servidores salvos.\n"
            "  --allow-local-http  Aceita HTTP sem TLS somente em localhost, 127.0.0.1\n"
            "                      e [::1], para servidores de desenvolvimento. Os\n"
            "                      demais endereços continuam exigindo HTTPS.\n"
            "  --help              Mostra esta ajuda e sai.\n"
            "  --version           Mostra a versão e sai.\n"),
          stdout);
    fputs(_("\nVariáveis de ambiente:\n"
            "  PAPO_LANGUAGE=pt|en   Idioma da interface quando a opção Idioma está em\n"
            "                        Sistema. Sem ela, segue o idioma do sistema.\n"
            "  CURL_CA_BUNDLE=ARQ    Arquivo PEM com a autoridade certificadora de um\n"
            "                        servidor com certificado próprio. A verificação\n"
            "                        TLS continua ativa.\n"),
          stdout);
#ifdef _WIN32
    fputs(_("\nArquivos:\n"
            "  %LOCALAPPDATA%\\papo-tui         Perfis e preferências.\n"
            "  %LOCALAPPDATA%\\papo-tui\\state   Sessões, leituras e rascunhos.\n"),
          stdout);
#else
    fputs(_("\nArquivos:\n"
            "  ~/.config/papo-tui       Perfis e preferências ($XDG_CONFIG_HOME).\n"
            "  ~/.local/state/papo-tui  Sessões, leituras e rascunhos ($XDG_STATE_HOME).\n"),
          stdout);
#endif
    fputs(_("  Senhas não são gravadas. Os cookies de sessão ficam no cofre de\n"
            "  credenciais do sistema.\n"
            "\n"
            "Guia de uso: https://github.com/prodesert22/papo-tui/blob/main/docs/usage.md\n"),
          stdout);
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
