# Instalação e uso

## Requisitos

Compilador C17, CMake 3.21+, pkg-config, ncursesw/formw 6.1+, libcurl 7.86+ com WS/WSS, TLS e DNS assíncrono, Jansson 2.14+ e libunibreak 6.1+. Use um terminal UTF-8 com pelo menos 44 colunas e 22 linhas disponíveis ao aplicativo. No tmux, reserve também as linhas da barra de status.

No Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson libunibreak
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

O tema usa fundo quase preto, painéis em cinza escuro e laranja `#ffb347` em títulos, seleções e foco, tanto no login quanto na demonstração. Terminais com paleta fixa usam uma aproximação do tom; marcadores de foco e seleção também permanecem visíveis sem cores.

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

## Demonstração do layout de conversas

```sh
./build/papo-tui --demo
```

Esse modo usa dados simulados, sem conectar a uma instância ou ler credenciais. A faixa superior contém abas de servidores. A coluna esquerda mostra os canais e DMs; o painel central reúne cabeçalho, histórico e editor.

O painel que recebe as teclas usa um fundo levemente mais claro, com diferença discreta dentro da paleta escura. Esse realce não acrescenta contornos coloridos nem rótulos aos painéis. O rodapé mostra o nome da área focada e os marcadores indicam a seleção. Ao abrir um modal, o realce ao fundo é desativado; o campo, os resultados ou o botão em foco também usam uma diferença suave de fundo.

| Controle | Ação na demonstração |
| --- | --- |
| Tab / Shift+Tab em tela ampla | Alternar entre abas, canais, histórico e editor |
| Tab / Shift+Tab em tela estreita | Alternar canais/conversa em uma tecla; na lista, abrir o canal destacado |
| ← / → nas abas | Trocar de servidor mantendo o foco nas abas; em **+ Adicionar**, Enter abre o aviso |
| ↓ nas abas | Focar os canais sem avançar a seleção |
| ↑ / ↓ nos canais | Percorrer canais/DMs; ↑ no primeiro canal volta às abas |
| ← nos canais | Voltar às abas |
| → nos canais | Abrir o canal destacado e focar seu histórico |
| ↑ / ↓ no histórico | Rolar uma linha mantendo o foco no histórico |
| ← no histórico | Voltar à lista de canais |
| Enter nas abas ou canais | Abrir a seleção |
| Clique esquerdo numa aba | Abrir o servidor ou acionar **+ Adicionar** |
| Clique esquerdo em `‹` / `›` | Mostrar abas fora da faixa visível |
| Clique esquerdo num canal ou DM | Abrir a conversa |
| Clique esquerdo no histórico | Focar o histórico |
| Digitar, colar texto ou Ctrl+N com foco no histórico | Focar o editor e inserir o conteúdo, preservando o rascunho |
| Clique esquerdo no campo do editor | Focar e posicionar o cursor na linha visível, respeitando grafemas |
| Clique esquerdo no rótulo do editor | Focar e posicionar o cursor no fim do rascunho |
| Clique em **Canais** / **Conversa** | Alternar os painéis em janelas estreitas |
| PageUp / PageDown, roda do mouse sobre a conversa | Rolar o histórico visível |
| Home / End com foco no histórico | Ir ao início/fim |
| F2 | Simular a chegada de uma mensagem na conversa atual |
| Clique no indicador de novas mensagens | Voltar ao fim do histórico |
| ← / → no editor | Mover entre grafemas |
| ↑ / ↓, Home / End no editor | Mover entre linhas; ir ao início/fim da linha visual |
| Backspace / Delete, Ctrl+U | Apagar um grafema ou limpar todo o rascunho |
| Ctrl+N no editor | Inserir uma quebra de linha |
| Enter no editor | Adicionar a mensagem somente à demonstração |
| Enter no histórico | Focar o editor; no estado de erro, refazer o carregamento simulado |
| F1 | Abrir ou fechar ajuda |
| Ctrl+K | Abrir navegação rápida por nome de servidor, canal ou DM |
| Ctrl+R, Enter no histórico de erro ou clique em **Tentar novamente** | Refazer o carregamento simulado |
| Ctrl+Q | Sair; abrir confirmação quando houver rascunhos |
| Esc | Fechar modal; sair do editor para o histórico; alternar histórico/lista de canais |
| Ctrl+C | Fechar diretamente |

Use ao menos 44 colunas e 14 linhas. A partir de 80 colunas, os dois painéis aparecem juntos. Abaixo dessa largura, Tab ou Shift+Tab alternam entre canais e conversa em uma única tecla, mantendo as abas no topo. Se o editor estiver focado, Tab volta diretamente aos canais. Redimensionar mantém seleções e rascunhos; trocar de conversa preserva seu rascunho e posição no histórico em memória.

As setas seguem o painel em foco. Nas abas, ←/→ trocam de servidor imediatamente e ↓ entra nos canais, sem avançar para outro canal. Na lista, ↑/↓ movem o destaque, ← volta às abas e →/Enter abrem o canal destacado. ↑ no primeiro canal também volta às abas. No histórico, ↑/↓ rolam as mensagens e ← volta aos canais. Esse fluxo também funciona no layout estreito, alternando o painel visível. Trocar o foco preserva o destaque da lista, o rascunho e a leitura da conversa.

Esc no histórico volta à lista; Esc na lista retorna à conversa atual sem abrir outra seleção. Esc no editor tira o foco de edição. No editor, as setas continuam movendo o cursor pelo texto; nos modais, navegam dentro do modal.

Depois de abrir um canal por clique, Enter ou navegação rápida, você pode começar a digitar diretamente: o foco muda do histórico para o editor e o primeiro caractere já entra no rascunho. A digitação continua na posição de cursor salva nessa conversa. Colagem delimitada e Ctrl+N seguem o mesmo comportamento; Enter no histórico também foca o editor, exceto no erro recuperável. Modais recebem sua própria entrada.

O mouse depende do suporte do terminal. Apenas o botão esquerdo ativa os controles; soltar o botão não repete a ação. Cliques fora dos controles ou durante colagem delimitada são ignorados. A roda também rola a ajuda e percorre resultados na navegação rápida.

Na colagem delimitada, CRLF e CR tornam-se uma única quebra de linha, e tabulações tornam-se espaços. Controles e atalhos dentro da colagem não enviam mensagens nem trocam de conversa. O conteúdo permanece no rascunho até pressionar Enter depois da colagem.

O editor aceita várias linhas e até 2048 caracteres Unicode (8192 bytes UTF-8). Ctrl+N insere uma quebra de linha. Colagem delimitada preserva quebras de linha, converte tabulações em espaços e nunca dispara envio; caracteres de controle são ignorados. Ao atingir o limite, a colagem é interrompida no limite do campo e a tela informa isso. Setas, Backspace e Delete preservam grafemas completos, incluindo acentos combinados. Sequências que a tela não consegue representar, como alguns emojis compostos, aparecem como `?`; o rascunho e a mensagem simulada mantêm o texto original.

Enter adiciona a mensagem ao histórico local e limpa o rascunho somente quando consegue concluir a operação. Cada conversa aceita até 32 mensagens locais, incluindo as chegadas simuladas por F2. Rolar para cima mantém seu ponto de leitura ao chegar uma mensagem; o indicador no cabeçalho informa quantas mensagens foram adicionadas e permite voltar ao fim. Redimensionar mantém a leitura ancorada na mensagem em exibição.

Na instância **Comunidade**, **ajuda** começa vazia, **ideias** demonstra carregamento e **eventos** começa com um erro recuperável. Esses estados e as tentativas são simulados e não enviam requisições de rede.

Em **Ctrl+K**, digite parte do nome, use ↑/↓ para selecionar e Enter para abrir; clicar no resultado também abre a conversa. Tab alterna entre filtro e resultados, e Esc volta sem trocar a conversa. A busca aceita até 128 caracteres e diferencia acentos.

Ctrl+Q abre uma confirmação quando existe qualquer rascunho. **Voltar** é a opção inicial; Tab ou ←/→ escolhem outra opção, e Enter confirma. Os botões também aceitam clique. Pressionar Ctrl+Q novamente confirma a saída. Ctrl+C e sinais de encerramento fecham diretamente. Todos os rascunhos e mensagens locais são descartados ao sair. **+ Adicionar** abre um aviso de que o cadastro de URLs ainda não está disponível.

## Problemas ao conectar

Confira a URL, a disponibilidade do servidor e as credenciais da conta nessa instância. Em servidores privados, confira também a senha do servidor. A tela informa erros de autenticação e rede.

Se o servidor redireciona a URL, informe o endereço final. Para HTTPS, o certificado precisa ser válido e confiável no sistema. Você pode cancelar uma tentativa com Esc e tentar novamente.

Se sua instância usa uma autoridade certificadora própria, indique seu certificado CA:

```sh
CURL_CA_BUNDLE=/caminho/ca.pem ./build/papo-tui
```

A validação do certificado e do nome do servidor permanece ativa.

Se a tela informar que o limite de requisições foi atingido, aguarde e tente novamente. O aplicativo respeita a espera indicada pelo servidor em `Retry-After` (até 24 horas); quando esse valor está ausente ou inválido, aguarda 3 segundos. A tentativa anterior não é repetida automaticamente.
