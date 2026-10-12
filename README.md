# Papo TUI

Cliente de conversas para terminal, em C, para conectar-se a instâncias independentes do Papo por URL.

A primeira entrega é uma tela de login com quatro campos: URL do servidor, usuário, senha e senha do servidor. A tela organiza cabeçalho, formulário e estado/controles em três áreas. O aplicativo consulta a instância, autoriza o acesso privado quando necessário, autentica a conta e confirma a sessão. As senhas são ocultas e os cookies ficam somente em memória.

## Compilar e executar

Pré-requisitos: compilador C17, CMake 3.21+, pkg-config, ncursesw/formw 6.1+, libcurl 7.86+ e Jansson 2.14+. A libcurl precisa oferecer WS/WSS, TLS e DNS assíncrono; o CMake verifica essas capacidades. Para os testes, Python 3.10+, OpenSSL CLI e as dependências de `requirements-dev.txt`.

No Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson python
```

```sh
cmake -S . -B build
cmake --build build
./build/papo-tui
```

Use um terminal UTF-8, com pelo menos 44 colunas e 22 linhas disponíveis ao aplicativo. No tmux, reserve também as linhas da barra de status. Para compilar somente o aplicativo, use `-DBUILD_TESTING=OFF` na configuração do CMake. `--help` e `--version` funcionam fora de um terminal interativo.

## Login

Informe uma URL HTTPS completa, como `https://chat.exemplo.com`. Para desenvolvimento local, HTTP é aceito em `localhost`, `127.0.0.1` e `[::1]`, com porta opcional. Prefixos de caminho são preservados, como em `https://exemplo.com/papo`.

A senha do servidor é obrigatória quando a instância se identifica como privada. Em instâncias públicas, deixe o campo vazio. **Entrar** consulta `/server`, faz `/auth/login_server` quando necessário, autentica por `/auth/login` e confirma a conta por `/auth/whoami`.

Depois de autenticar, a tela apresenta a conta, a instância e o estado da conexão de tempo real, e permite **Sair da conta**. O aplicativo abre um WebSocket autenticado com os cookies dessa sessão e recebe eventos em memória. A interface de conversas será construída nos próximos marcos. Login e senha não são persistidos pelo aplicativo.

A sessão é revalidada por REST a cada 30 segundos; heartbeat/ACK do WebSocket não comprova autenticação. Falhas de tempo real aparecem na área de estado. Para refazer uma conexão interrompida, saia da conta e entre novamente.

As requisições compartilham um limite global de 10 por segundo, com burst de 25. O envio aguarda capacidade sem bloquear a interface. HTTP 429 informa o limite atingido e adia novas tentativas conforme `Retry-After`, sem reenviar a solicitação automaticamente.

| Tecla | Ação |
| --- | --- |
| Tab / Shift+Tab, ↑ / ↓ | Alternar campo ou botão |
| Enter | Entrar; sair da conta quando autenticado |
| ← / →, Home / End | Mover cursor no campo |
| Backspace / Delete | Apagar texto |
| Clique esquerdo no campo | Focar e posicionar o cursor |
| Clique esquerdo no botão | Entrar ou sair da conta |
| Ctrl+U | Limpar o campo atual |
| Esc | Cancelar uma conexão em andamento |
| Ctrl+Q / Ctrl+C | Sair do aplicativo |

Backspace no início do campo mantém o foco; segurar a tecla não navega entre inputs. Em terminais com suporte a mouse, clique no campo para editar naquele ponto ou no rótulo para editar ao final. Cliques fora dos controles são ignorados; durante a conexão, os controles ficam bloqueados.

Colagem delimitada pelo terminal não envia com quebras de linha. Redimensionar preserva os dados; em janela menor que o mínimo, o aplicativo solicita ampliação. Erros de autenticação e rede são apresentados na tela.

## Objetivo

A primeira versão, v0.1.0, permitirá cadastrar várias instâncias, entrar em uma delas e conversar por canais de texto e mensagens diretas, com histórico, tempo real e reconexão. O login será solicitado a cada abertura. Cada instância mantém suas próprias contas, conversas e sessão. A API atual não especifica federação entre servidores.

Consulte [instalação e uso](docs/uso.md) para os requisitos, execução, conexão e solução de problemas.

## Stack e estrutura

- C17 e CMake.
- ncursesw e formw para renderização, navegação e edição dos campos.
- libcurl multi para HTTP e handshake WS assíncronos, TLS, URLs e cookies; APIs WS para leitura/escrita sem bloquear a interface.
- Jansson para JSON.

```text
src/main.c       Entrada e ciclo de vida
src/ui/          Tela de login e formulário
src/net/         Estado de autenticação e transporte HTTP
tests/           Backend simulado e testes em pseudoterminal
docs/            Instalação e uso do programa
openapi.yml      Contrato do backend
```

## API

Use [`openapi.yml`](openapi.yml) como referência para endpoints, autenticação, modelos e eventos WebSocket. Confirme o contrato antes de implementar qualquer comportamento do cliente; não presuma que a API oferece recursos que não estão descritos.

As ferramentas em `tools/` auxiliam a validação do contrato durante o desenvolvimento. Seus relatórios e inventários são gerados em `build/`; não fazem parte da documentação de uso.

## Estado atual

- [x] Especificação OpenAPI disponível.
- [x] Tela de login, HTTP e aplicativo compilável.
- [x] Testes com backend simulado e pseudoterminal.
- [x] Inventário e fixtures de contrato com validação OpenAPI 3.0.
- [x] Login público, sessão e handshake WS nativo observados na instância local.
- [x] R0: contrato e ambiente de integração validados com a conta fornecida, paginação e cenário privado HTTPS/WSS.
- [x] Transporte WS/WSS em C, com cookies por contexto, filas limitadas e revalidação REST.
- [x] Base de terminal exercitada em Kitty, Konsole e tmux.
- [ ] Interface de conversas.

## Testes

```sh
python -m venv build/test-env
build/test-env/bin/python -m pip install -r requirements-dev.txt
cmake -S . -B build -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
```

Os testes usam credenciais sintéticas e servidores HTTP/WS locais. Cobrem acesso público/privado, cookies, logout, erros, limites de entrada/resposta/fila, cancelamento, URLs com prefixo, senhas ocultas, espaços em senhas, redimensionamento, Backspace repetido, cliques e restauração do terminal. pyte reconstrói a tela para verificar a posição do cursor. A suíte WS inclui fragmentação dentro de UTF-8, chunks, ping/pong intercalado, encerramento, timeouts, sessão revogada com ACK e WSS com CA/hostname verificados. Essas dependências Python são usadas somente nos testes; os mocks não comprovam integração com a instância real.

A suíte `api-contract` valida 51 fixtures REST e 17 exemplos WS, cursor composto com timestamps iguais, limites de lote, YAML 1.2, isolamento de cookies e relatórios sem segredos. Regerar o inventário local e os exemplos após uma mudança no contrato:

```sh
build/test-env/bin/python -m tools.contract_inventory
build/test-env/bin/python -m tools.contract_fixtures
```

O inventário fica em `build/contract/api-inventory.md`. Para verificar a instância de desenvolvimento com uma conta fornecida, defina sua senha na variável de ambiente `PAPO_TEST_PASSWORD` e execute:

```sh
build/test-env/bin/python -m tools.check_backend --url http://127.0.0.1:8080 --username SEU_USUARIO --auth-probe ./build/auth-probe --request-interval 0.25
```

Nesse modo, a senha fica somente no ambiente e nenhuma conta é cadastrada. Metadados sem senhas ou cookies ficam em `build/integration/report.json`. O probe revoga somente sua conexão de teste e tenta logout ao terminar. `--prepare-history` cria um canal dedicado com 102 mensagens e guarda apenas IDs em `build/integration/fixture-state.json`; use essa opção quando quiser preparar dados no servidor. `--ca-bundle` indica uma CA confiável para HTTPS/WSS. Consulte `--help` para as opções disponíveis.

Para repetir a validação de R1, defina `PAPO_TEST_USERNAME` e `PAPO_TEST_PASSWORD`. Se mantiver essas variáveis em um `.env` local, carregue-o no shell:

```sh
set -a
. ./.env
set +a
go build -o build/integration/tls-proxy tools/tls_proxy.go
build/test-env/bin/python -m tools.check_r1
```

Esse probe usa o cliente C com a conta indicada, faz logout das sessões de teste e valida HTTP/WS e HTTPS/WSS por proxy TLS local. Go e OpenSSL CLI são necessários para esse cenário. A CA e a chave temporárias são removidas; somente resultados sem segredos ficam em `build/integration/r1-backend.json`. `.env` é ignorado pelo Git e deve ter permissão `600`.

Com Kitty, Konsole, tmux, qdbus6 e xdotool disponíveis numa sessão gráfica X11/XWayland, a validação dos emuladores usa janelas dedicadas e fixtures sintéticas:

```sh
build/test-env/bin/python -m tools.check_terminals --tmux /usr/bin/tmux
```

Os resultados e as telas ficam em `build/r1/terminals/`. A ferramenta usa uma configuração temporária de Konsole e um servidor tmux isolado; fecha suas janelas ao terminar.

```sh
cmake -S . -B build/sanitized -DPAPO_SANITIZERS=ON -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build/sanitized
ctest --test-dir build/sanitized --output-on-failure
```

Em ambientes sob ptrace, o LeakSanitizer não funciona. Nesses ambientes, execute `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitized --output-on-failure`: AddressSanitizer e UndefinedBehaviorSanitizer continuam ativos, mas vazamentos não são verificados.
