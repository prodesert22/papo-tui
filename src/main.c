#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "ui/login.h"

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("papo-tui " PAPO_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Uso: papo-tui [--help | --version]\n"
             "Abre a tela de login. Tab: foco; Enter: entrar; Esc: cancelar; Ctrl+Q: sair.");
        return 0;
    }
    if (argc != 1) {
        fputs("Argumento desconhecido. Use papo-tui --help.\n", stderr);
        return 1;
    }
    if (!setlocale(LC_ALL, "")) {
        fputs("Não foi possível inicializar a localidade do terminal.\n", stderr);
        return 1;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fputs("Não foi possível inicializar a comunicação.\n", stderr);
        return 1;
    }
    int result = login_run();
    curl_global_cleanup();
    return result;
}
