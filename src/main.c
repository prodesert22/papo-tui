#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "ui/login.h"
#include "ui/chat.h"

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("papo-tui " PAPO_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Uso: papo-tui [--demo | --help | --version]\n"
             "Sem argumentos: tela de login.\n"
             "--demo: layout de conversas com dados simulados, sem conexão.\n"
             "Tab: foco; Enter: confirmar; Ctrl+Q: sair.");
        return 0;
    }
    const int demo = argc == 2 && strcmp(argv[1], "--demo") == 0;
    if (argc != 1 && !demo) {
        fputs("Argumento desconhecido. Use papo-tui --help.\n", stderr);
        return 1;
    }
    if (!setlocale(LC_ALL, "")) {
        fputs("Não foi possível inicializar a localidade do terminal.\n", stderr);
        return 1;
    }
    if (demo) return chat_demo_run();
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fputs("Não foi possível inicializar a comunicação.\n", stderr);
        return 1;
    }
    int result = login_run();
    curl_global_cleanup();
    return result;
}
