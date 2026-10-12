# Instalação e uso

## Requisitos

Compilador C17, CMake 3.21+, pkg-config, ncursesw/formw 6.1+, libcurl 7.86+ com WS/WSS, TLS e DNS assíncrono, e Jansson 2.14+. Use um terminal UTF-8 com pelo menos 44 colunas e 22 linhas disponíveis ao aplicativo. No tmux, reserve também as linhas da barra de status.

No Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson
```

## Compilar e executar

Na raiz do projeto:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
./build/papo-tui
```

O executável fica em `build/papo-tui`. Para consultar as opções ou a versão:

```sh
./build/papo-tui --help
./build/papo-tui --version
```

## Conectar ao servidor

A tela apresenta um cabeçalho, o formulário e uma área de estado e controles. Preencha os quatro campos:

| Campo | O que informar |
| --- | --- |
| URL do servidor | URL completa, como `https://chat.exemplo.com` |
| Usuário | Seu username nessa instância |
| Senha | A senha da sua conta |
| Senha do servidor | Obrigatória em instâncias privadas; deixe vazia em instâncias públicas |

Para um servidor local, use `http://127.0.0.1:8080`. HTTP também é aceito em `localhost` e `[::1]`; servidores remotos exigem HTTPS. Prefixos de caminho são preservados, como em `https://exemplo.com/papo`.

Selecione **Entrar** ou pressione Enter. O aplicativo consulta o servidor, solicita acesso à instância privada quando necessário e autentica sua conta. As senhas são ocultas; credenciais e cookies não são salvos entre execuções.

Depois de autenticar, a tela mostra a conta, o servidor e o estado da conexão de tempo real. **Sair da conta** encerra essa conexão e a sessão. A versão atual implementa login, logout e recebimento de eventos em memória; a tela de conversas ainda não está disponível.

A sessão é confirmada novamente a cada 30 segundos. Se a confirmação falhar, o aplicativo limpa a sessão e solicita novo login. Para refazer uma conexão de tempo real interrompida, saia da conta e entre novamente.

O aplicativo compartilha um limite de 10 requisições por segundo, com capacidade de burst de 25, entre todas as tentativas de conexão. Ao atingir esse limite, os envios aguardam e a interface continua respondendo. Esses valores são fixos nesta versão.

## Teclado e mouse

| Controle | Ação |
| --- | --- |
| Tab / Shift+Tab, ↑ / ↓ | Alternar campo ou botão |
| Enter | Entrar ou sair da conta |
| ← / →, Home / End | Mover o cursor no campo |
| Backspace / Delete | Apagar texto |
| Ctrl+U | Limpar o campo atual |
| Clique esquerdo no campo | Focar e posicionar o cursor |
| Clique esquerdo no botão | Entrar ou sair da conta |
| Esc | Cancelar uma conexão em andamento |
| Ctrl+Q / Ctrl+C | Fechar o aplicativo |

Backspace no início do campo mantém o foco. Cliques fora dos controles são ignorados. Durante a conexão, os campos ficam bloqueados. O suporte a mouse depende do terminal.

Redimensionar a janela preserva os campos. Se o terminal ficar menor que o mínimo, amplie-o para continuar. Colagem delimitada pelo terminal não envia o formulário ao encontrar uma quebra de linha.

## Problemas ao conectar

Confira a URL, a disponibilidade do servidor e as credenciais da conta nessa instância. Em servidores privados, confira também a senha do servidor. A tela informa erros de autenticação e rede.

Se o servidor redireciona a URL, informe o endereço final. Para HTTPS, o certificado precisa ser válido e confiável no sistema. Você pode cancelar uma tentativa com Esc e tentar novamente.

Se sua instância usa uma autoridade certificadora própria, indique seu certificado CA:

```sh
CURL_CA_BUNDLE=/caminho/ca.pem ./build/papo-tui
```

A validação do certificado e do nome do servidor permanece ativa.

Se a tela informar que o limite de requisições foi atingido, aguarde e tente novamente. O aplicativo respeita a espera indicada pelo servidor em `Retry-After` (até 24 horas); quando esse valor está ausente ou inválido, aguarda 3 segundos. A tentativa anterior não é repetida automaticamente.
