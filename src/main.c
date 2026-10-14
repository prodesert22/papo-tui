#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "ui/login.h"
#include "model/mentions.h"

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("papo-tui " PAPO_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Uso: papo-tui [--profiles] [--allow-local-http] | --help | --version\n"
             "Sem argumentos: recuperar a última sessão ou abrir login.\n"
             "--profiles: gerenciar servidores salvos antes do login.\n"
             "--allow-local-http: permitir HTTP de desenvolvimento em loopback.\n"
             "Ctrl+P: selecionar perfil; Ctrl+L no gerenciador: limites; F5: identificar instância.\n"
             "Tab: foco; Enter: confirmar; Ctrl+Q: sair.");
        return 0;
    }
    bool profiles = false, allow_http = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--profiles") == 0 && !profiles) profiles = true;
        else if (strcmp(argv[i], "--allow-local-http") == 0 && !allow_http) allow_http = true;
        else {
            fputs("Argumento desconhecido ou repetido. Use papo-tui --help.\n", stderr);
            return 1;
        }
    }
    if (!setlocale(LC_ALL, "")) {
        fputs("Não foi possível inicializar a localidade do terminal.\n", stderr);
        return 1;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fputs("Não foi possível inicializar a comunicação.\n", stderr);
        return 1;
    }
    int result = login_run_profiles(profiles, allow_http);
    mention_cleanup();
    curl_global_cleanup();
    return result;
}
