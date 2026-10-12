# Papo TUI

Cliente de conversas para terminal, em C, para conectar-se a instâncias independentes do Papo por URL.

A primeira entrega é uma tela de login com quatro campos: URL do servidor, usuário, senha e senha do servidor. O aplicativo consulta a instância, autoriza o acesso privado quando necessário, autentica a conta e confirma a sessão. As senhas são ocultas e os cookies ficam somente em memória.

## Compilar e executar

Pré-requisitos: compilador C17, CMake 3.21+, pkg-config, ncursesw/formw 6.1+, libcurl 7.85+ e Jansson 2.14+. Para os testes, Python 3.10+ e as dependências de `requirements-dev.txt`.

No Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson python
```

```sh
cmake -S . -B build
cmake --build build
./build/papo-tui
```

Use um terminal UTF-8, com pelo menos 44 colunas e 22 linhas. Para compilar somente o aplicativo, use `-DBUILD_TESTING=OFF` na configuração do CMake. `--help` e `--version` funcionam fora de um terminal interativo.

## Login

Informe uma URL HTTPS completa, como `https://chat.exemplo.com`. Para desenvolvimento local, HTTP é aceito em `localhost`, `127.0.0.1` e `[::1]`, com porta opcional. Prefixos de caminho são preservados, como em `https://exemplo.com/papo`.

A senha do servidor é obrigatória quando a instância se identifica como privada. Em instâncias públicas, deixe o campo vazio. **Entrar** consulta `/server`, faz `/auth/login_server` quando necessário, autentica por `/auth/login` e confirma a conta por `/auth/whoami`.

Depois de autenticar, a tela apresenta a conta e a instância e permite **Sair da conta**. A interface de conversas será construída nos próximos marcos. Login e senha não são persistidos.

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

O próximo trabalho é validar o fluxo na instância real e completar a verificação do contrato. As decisões técnicas e o comportamento implementado estão documentados em [docs/decisoes.md](docs/decisoes.md) e [docs/contrato.md](docs/contrato.md).

## Stack e estrutura

- C17 e CMake.
- ncursesw e formw para renderização, navegação e edição dos campos.
- libcurl multi para HTTP assíncrono, TLS, URLs e cookies.
- Jansson para JSON.

```text
src/main.c       Entrada e ciclo de vida
src/ui/          Tela de login e formulário
src/net/         Estado de autenticação e transporte HTTP
tests/           Backend simulado e testes em pseudoterminal
docs/            Contrato e decisões técnicas
openapi.yml      Contrato do backend
```

A escolha de bibliotecas está em [docs/decisoes.md](docs/decisoes.md), e o contrato inicial de autenticação em [docs/contrato.md](docs/contrato.md).

## API

Use [`openapi.yml`](openapi.yml) como referência para endpoints, autenticação, modelos e eventos WebSocket. Confirme o contrato antes de implementar qualquer comportamento do cliente; não presuma que a API oferece recursos que não estão descritos.

## Estado atual

- [x] Especificação OpenAPI disponível.
- [x] Plano inicial e roadmap registrados.
- [x] Tela de login, HTTP e aplicativo compilável.
- [x] Testes com backend simulado e pseudoterminal.
- [ ] Integração com a instância real e validação completa de R0.
- [ ] Interface de conversas e WebSocket.

## Testes

```sh
python -m venv build/test-env
build/test-env/bin/python -m pip install -r requirements-dev.txt
cmake -S . -B build -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
```

Os testes usam credenciais sintéticas e servidor HTTP local. Cobrem acesso público/privado, cookies, logout, erros, limites de resposta, cancelamento, URLs com prefixo, senhas ocultas, espaços em senhas, redimensionamento, Backspace repetido, cliques e restauração do terminal. pyte reconstrói a tela para verificar a posição do cursor. Essas dependências Python são usadas somente nos testes; eles não comprovam integração com a instância real.

```sh
cmake -S . -B build/sanitized -DPAPO_SANITIZERS=ON -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build/sanitized
ctest --test-dir build/sanitized --output-on-failure
```

Em ambientes sob ptrace, o LeakSanitizer não funciona. Nesses ambientes, execute `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitized --output-on-failure`: AddressSanitizer e UndefinedBehaviorSanitizer continuam ativos, mas vazamentos não são verificados.
