# Papo TUI

Cliente de conversas para terminal, em C, para conectar-se a instâncias independentes do Papo por URL.

A primeira entrega é uma tela de login com quatro campos: URL do servidor, usuário, senha e senha do servidor. A tela organiza cabeçalho, formulário e estado/controles em três áreas. O aplicativo consulta a instância, autoriza o acesso privado quando necessário, autentica a conta e confirma a sessão. As senhas são ocultas e os cookies ficam somente em memória.

## Compilar e executar

Pré-requisitos: compilador C17, CMake 3.21+, pkg-config, ncursesw/formw 6.1+, libcurl 7.86+, Jansson 2.14+ e libunibreak 6.1+. A libcurl precisa oferecer WS/WSS, TLS e DNS assíncrono; o CMake verifica essas capacidades. Para os testes, Python 3.10+, OpenSSL CLI e as dependências de `requirements-dev.txt`.

No Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson libunibreak python
```

```sh
cmake -S . -B build
cmake --build build
./build/papo-tui
```

Para abrir o layout de conversas com dados simulados:

```sh
./build/papo-tui --demo
```

A demonstração mostra servidores em abas no topo, canais/DMs à esquerda e histórico com editor no painel central. Não abre conexão nem usa credenciais. As setas atuam no painel em foco: nas abas, ←/→ trocam de servidor e ↓ entra nos canais; nos canais, ↑/↓ escolhem a conversa, →/Enter a abrem e ← volta às abas. No primeiro canal, ↑ também volta às abas. No histórico, ↑/↓ rolam uma linha e ← retorna aos canais. Tab/Shift+Tab alternam o foco em tela ampla. Em menos de 80 colunas, uma única pressão de Tab ou Shift+Tab alterna entre canais e conversa; na lista, essa ação abre o canal destacado. PageUp/PageDown rolam o histórico. Esse modo exige pelo menos 44 colunas e 14 linhas.

Em terminais com suporte a mouse, clique numa aba ou canal para abri-lo, no histórico para focá-lo e no editor para posicionar o cursor. As setas `‹`/`›` mostram abas fora da faixa visível. Em janelas estreitas, os controles **Canais** e **Conversa** alternam os painéis. Cliques fora dos controles e eventos de soltura são ignorados.

Depois de abrir um canal, basta digitar para focar o editor e começar a mensagem, incluindo o primeiro caractere. Colagem delimitada e Ctrl+N também focam o editor a partir do histórico, preservando o rascunho e sua posição do cursor.

Esc sai do editor para o histórico; no histórico, Esc volta à lista de canais. Na lista, Esc retorna à conversa atual sem confirmar outra seleção. Com o editor focado, as setas editam o texto; os demais painéis usam as setas para sua própria navegação. Enter no histórico abre o editor, ou refaz o carregamento quando há um erro simulado. Modais mantêm seus próprios controles de navegação.

O login e a demonstração usam um tema escuro: fundo quase preto, painéis em cinza escuro e destaque laranja `#ffb347`. O tom exato depende do suporte a RGB ou à redefinição de cores do terminal; paletas fixas usam a aproximação disponível. No Konsole com terminfo de 256 cores, a paleta fixa preserva o tema escuro e usa o laranja aproximado `#ffaf5f`.

Na demonstração, o foco é indicado por um fundo discretamente mais claro, mantendo os tons escuros da paleta. O rodapé informa qual área recebe as teclas; os marcadores da seleção continuam presentes. Ao abrir um modal, o realce dos painéis ao fundo é desativado.

O editor aceita várias linhas e mantém rascunhos por servidor e conversa, com limite de 2048 caracteres Unicode (até 8192 bytes UTF-8). Ctrl+N insere uma quebra de linha; colagem delimitada preserva linhas e nunca envia automaticamente. Enter adiciona a mensagem somente à demonstração. Setas, Backspace e Delete operam sobre grafemas inteiros. Sequências compostas que o ncurses não representa são exibidas como `?`, preservando o texto original.

F1 abre a ajuda e Ctrl+K filtra servidores, canais e DMs por nome. PageUp/PageDown ou a roda do mouse rolam o histórico; Home/End com foco no histórico vão ao início/fim. F2 simula a chegada de uma mensagem sem deslocar a leitura quando você estiver vendo mensagens anteriores; o indicador de novas mensagens permite voltar ao fim. Cada conversa aceita até 32 mensagens locais, somando envio e chegada simulada.

Há exemplos de canal vazio, carregamento e erro recuperável: na primeira instância, **ajuda**, **ideias** e **eventos**, respectivamente. No erro, Ctrl+R, Enter com foco no histórico ou clique em **Tentar novamente** repetem o carregamento simulado. Ctrl+Q abre uma confirmação quando há rascunhos; Ctrl+C fecha diretamente. Os dados são descartados ao sair. Cadastro de servidores e integração de mensagens com o backend permanecem nas próximas entregas.

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
- ncursesw para renderização, navegação e painéis; formw para os campos de login.
- libunibreak para delimitar grafemas no editor de mensagens e na apresentação de texto.
- libcurl multi para HTTP e handshake WS assíncronos, TLS, URLs e cookies; APIs WS para leitura/escrita sem bloquear a interface.
- Jansson para JSON.

```text
src/main.c       Entrada e ciclo de vida
src/ui/          Login, conversas simuladas, tema e edição Unicode
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
- [x] R2: interface navegável com dados simulados em `--demo`, editor multilinha, mouse e ajuda.
- [ ] Interface de conversas integrada ao backend.

## Testes

```sh
python -m venv build/test-env
build/test-env/bin/python -m pip install -r requirements-dev.txt
cmake -S . -B build -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
```

Os testes usam credenciais sintéticas e servidores HTTP/WS locais. Cobrem acesso público/privado, cookies, logout, erros, limites de entrada/resposta/fila, cancelamento, URLs com prefixo, senhas ocultas, espaços em senhas, redimensionamento, Backspace repetido, cliques e restauração do terminal. pyte reconstrói a tela para verificar a posição do cursor. A suíte WS inclui fragmentação dentro de UTF-8, chunks, ping/pong intercalado, encerramento, timeouts, sessão revogada com ACK e WSS com CA/hostname verificados. Essas dependências Python são usadas somente nos testes; os mocks não comprovam integração com a instância real.

As suítes `unicode-editor` e `chat-terminal` exercitam a demonstração local: grafemas, largura em células, cursor nas linhas quebradas, colagem multilinha e fragmentada, limites, mouse, atalhos, modais, estados vazio/carregando/erro, isolamento de rascunhos, preservação da leitura, resize e saída. Também conferem os indicadores de foco e erro em terminal sem cores.

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
build/test-env/bin/python -m tools.check_terminals --demo --tmux /usr/bin/tmux
```

Os resultados e as telas ficam em `build/r1/terminals/` para o login e `build/r2/terminals/` para a demonstração. A ferramenta usa uma configuração temporária de Konsole e um servidor tmux isolado; fecha suas janelas ao terminar. Eventos de mouse são enviados à PTY no protocolo declarado pelo terminfo; `infocmp` também deve estar disponível. A opção `--demo` usa somente dados locais, sem credenciais ou conexão ao backend.

```sh
cmake -S . -B build/sanitized -DPAPO_SANITIZERS=ON -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build/sanitized
ctest --test-dir build/sanitized --output-on-failure
```

Em ambientes sob ptrace, o LeakSanitizer não funciona. Nesses ambientes, execute `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitized --output-on-failure`: AddressSanitizer e UndefinedBehaviorSanitizer continuam ativos, mas vazamentos não são verificados.
