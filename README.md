# Papo TUI

Cliente de conversas para terminal, em C, para conectar-se a instâncias independentes do Papo por URL.

O aplicativo oferece login, cadastro de conta, renovação de sessão, perfis locais com nome e URL e conversas em canais com histórico paginado e envio de texto. Login e cadastro usam URL do servidor, usuário, senha e senha do servidor. A tela organiza cabeçalho, formulário e estado/controles em três áreas. O aplicativo consulta a instância, autoriza o acesso privado quando necessário, autentica a conta e confirma a sessão. As senhas são ocultas e nunca são persistidas. A última sessão pode ser recuperada com GNOME Keyring ou KWallet.

## Compilar e executar

Pré-requisitos: compilador C17, CMake 3.21+, pkg-config, ncursesw/formw 6.1+, libcurl 7.86+, Jansson 2.14+, libunibreak 6.1+, libsixel 1.8+, libchafa 1.16+, GLib 2.66+, libmpv (API 2.0+) e bibliotecas de desenvolvimento do FFmpeg (libavformat/libavcodec 58+, libavutil 56+, libswscale 5+). A libcurl precisa oferecer WS/WSS, TLS e DNS assíncrono; o CMake verifica essas capacidades. Para os testes, Python 3.10+, FFmpeg CLI, OpenSSL CLI e as dependências de `requirements-dev.txt`.

No Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson libunibreak libsixel chafa glib2 mpv ffmpeg python
```

```sh
cmake -S . -B build
cmake --build build
./build/papo-tui
```

Use um terminal UTF-8, com pelo menos 44 colunas e 22 linhas disponíveis ao aplicativo. No tmux, reserve também as linhas da barra de status. Para compilar somente o aplicativo, use `-DBUILD_TESTING=OFF` na configuração do CMake. `--help` e `--version` funcionam fora de um terminal interativo.

## Login

### Perfis de instância

Ao iniciar normalmente, o aplicativo tenta recuperar a última sessão; se não puder, abre a tela de login. O botão **Selecionar perfil**, disponível por clique ou Tab e Enter, abre um modal com os perfis salvos; **Ctrl+P** é o atalho para esse modal. Use ↑/↓ e Enter, ou clique no perfil, para preencher o login. No modal, **Gerenciar / Ctrl+P** abre o cadastro e **Cancelar / Esc** volta ao login.

Para iniciar diretamente no gerenciador completo:

```sh
./build/papo-tui --profiles
```

O gerenciador tem duas opções no topo: **Listar** e **+ Adicionar**. Em **Listar**, os perfis aparecem numa lista vertical com rolagem automática: ↑/↓ mudam o destaque, PageUp/PageDown avançam uma página e Home/End vão ao início/fim. Enter ou → na lista levam ao primeiro botão do rodapé, **Selecionar**, seguido de **Editar**, **Remover**, **Limites** e **Identificar**. Confirme **Selecionar** para preencher o login. Nas ações, ←/→ movem entre os botões e param nas extremidades; ↑ volta à lista. ↑ no primeiro perfil volta ao topo; ali, ← vai para **Listar**, → para **Adicionar**, e ↓ ou Enter em **Listar** entram na lista. ← na lista mantém o foco. Tab/Shift+Tab também percorrem as áreas e botões. Clicar num perfil destaca-o; clicar num botão executa sua ação.

No gerenciador e nos modais de perfil, os textos principais e botões são brancos; o texto dos botões e itens de lista selecionados usa laranja `#ffb347`, e dicas e comandos permanecem cinza. A área em foco usa um fundo laranja escuro mais evidente, enquanto o perfil destacado fora do foco mantém um fundo laranja discreto. As dicas de navegação do rodapé acompanham a área focada.

Em terminais sem cores, o foco usa inversão e o perfil destacado fora do foco usa negrito. Em telas estreitas, o rodapé encurta os atalhos para manter os controles visíveis.

**+ Adicionar** abre o cadastro de nome e URL por clique ou Enter. Enquanto essa opção estiver selecionada, as ações do rodapé ficam indisponíveis, inclusive depois de cancelar o cadastro. Salvar um perfil volta à lista com ele destacado. **Editar** renomeia ou corrige a URL mantendo o ID local; **Remover** exige confirmação e começa em **Cancelar**. **Ctrl+N**, **Ctrl+E** e **Ctrl+D** abrem essas três ações. Esc cancela o diálogo ou volta ao login, e Ctrl+Q fecha o aplicativo; Ctrl+C limpa o campo de texto focado e fecha quando o foco está fora dos campos. Colagem delimitada não confirma ações automaticamente.

Ao abrir a seleção de perfil pelo login, a conexão de tempo real é encerrada e cookies, usuário e senhas locais são descartados. Selecionar outro perfil exige novo login. A URL do perfil fica bloqueada no formulário; altere-a em **Selecionar perfil → Gerenciar → Editar**. Os perfis contêm somente os dados públicos da instância.

Nome, URL e ID do perfil selecionado ficam em `$XDG_CONFIG_HOME/papo-tui/config.json`, ou `~/.config/papo-tui/config.json`. O diretório usa permissão `700` e o arquivo `600`. A gravação usa arquivo temporário e substituição atômica. Uma configuração inválida/incompatível é preservada e bloqueia alterações; corrija o arquivo e use **Ctrl+R** no gerenciador para recarregar. Se outro processo alterar a configuração, recarregue antes de salvar. Há limite de 64 perfis, nomes de 128 bytes UTF-8 e URLs de 2048 bytes.

Perfis exigem HTTPS. Para cadastrar ou selecionar uma URL HTTP de desenvolvimento em `localhost`, `127.0.0.1` ou `[::1]`, habilite explicitamente:

```sh
./build/papo-tui --profiles --allow-local-http
```

A URL base não aceita credenciais embutidas, query ou fragmento. Portas e prefixos de caminho são preservados. Ao destacar um perfil, o gerenciador consulta `/health` e `/server` e mostra o nome informado pela instância e se ela é pública ou privada. **Identificar / F5** repete a consulta. A interface continua respondendo durante a conexão ou espera pelo limite. Erros de DNS, timeout, conexão recusada, TLS, backend incompatível e instância não inicializada aparecem nessa tela; use **Editar** para corrigir a URL ou **F5** para tentar novamente. Essa identificação não autentica a conta.

### Limites de requisições

No gerenciador, destaque um perfil salvo para usar **Limites / Ctrl+L** e definir `requests_per_second` e `request_burst`. A taxa aceita um número positivo e finito, com ponto decimal; burst deve ser um inteiro entre 1 e 4294967295. **Padrão** preenche 10/25; **Salvar** persiste e aplica os valores ao cliente inteiro. Valores inválidos mantêm a configuração anterior.

As preferências são carregadas ao iniciar, inclusive no login manual. Salvar, recarregar ou trocar de perfil preserva o crédito existente e a espera de `Retry-After`; reduzir o burst limita o crédito restante à nova capacidade. Configurações da primeira etapa de R3 continuam sendo lidas com os padrões 10/25 e recebem o novo formato na próxima gravação.

### Acesso à conta

Informe uma URL HTTPS completa, como `https://chat.exemplo.com`. Para desenvolvimento local, inicie com `--allow-local-http`: HTTP é aceito em `localhost`, `127.0.0.1` e `[::1]`, com porta opcional. Essa opção também é necessária no login manual. Prefixos de caminho são preservados, como em `https://exemplo.com/papo`.

A senha do servidor é obrigatória quando a instância se identifica como privada. Em instâncias públicas, deixe o campo vazio. **Entrar** consulta `/server`, faz `/auth/login_server` quando necessário, autentica por `/auth/login` e confirma a conta por `/auth/whoami`.

Se faltar a senha do servidor numa instância privada, o campo **Senha do servidor** recebe uma borda vermelha e o foco para você digitar. A senha rejeitada pelo servidor também direciona o foco a esse campo e é apagada para uma nova tentativa. O destaque sai quando você começa a corrigir o campo; confirme com Enter para tentar novamente.

Depois de autenticar, a lista de canais abre automaticamente. Escolha um canal para ler suas mensagens. O aplicativo abre um WebSocket autenticado com os cookies dessa sessão e aplica mensagens, edições, exclusões e mudanças de canais em memória. F6 volta à tela da conta, onde você pode renovar ou sair da sessão; **Canais / F6** reabre a leitura. Login e senha não são persistidos pelo aplicativo.

**Cadastro / Ctrl+N** alterna entre login e criação de conta, preservando URL e usuário e limpando as senhas. O cadastro exige usuário com pelo menos 3 caracteres e senha com pelo menos 8 caracteres, uma letra maiúscula e um caractere especial; a instância pode impor limites adicionais. Repita a senha em **Confirmar senha**. Senhas diferentes ou confirmação vazia bloqueiam o envio e focam a confirmação. Senha curta, sem maiúscula ou sem caractere especial recebe uma mensagem específica e foco para correção. Em uma instância privada, informe também a senha do servidor. Depois de criar a conta, digite novamente a senha e confirme **Entrar**: cadastro não faz login automático. Usuário já existente recebe foco para correção; falhas de política exibem uma mensagem sem revelar detalhes remotos.

A sessão é revalidada por REST em intervalos de até 30 segundos; heartbeat/ACK do WebSocket não comprova autenticação. A primeira renovação ocorre até 30 segundos após o login e as seguintes são agendadas antes da expiração informada pelo servidor. **Renovar / Ctrl+R** antecipa a renovação. Falhas de autenticação, revogação, banimento ou identidade incompatível limpam a sessão e devolvem o foco à senha para novo login.

Falhas de tempo real aparecem na área de estado. A aba ativa tenta reconectar automaticamente, com esperas de 1, 2, 4, 8, 16 e até 30 segundos, com variação de até 20% e teto de 30 segundos. **Reconectar / Ctrl+R** antecipa a tentativa; a sessão é revalidada por REST antes de reabrir a conexão. Abas inativas mantêm conexões já abertas, mas aguardam voltar ao foco para iniciar novas tentativas. **Sair da conta** encerra a sessão local mesmo se a revogação remota falhar; a tela informa quando ela não foi confirmada. Logout durante uma renovação aguarda sua resposta para usar o cookie atualizado.

As requisições compartilham um limite global configurável, com padrão de 10 por segundo e burst de 25. O envio aguarda capacidade sem bloquear a interface. HTTP 429 informa o limite atingido e adia novas tentativas conforme `Retry-After`, sem reenviar a solicitação automaticamente.

| Tecla | Ação |
| --- | --- |
| Tab / Shift+Tab | Percorrer campos e botões na ordem visual |
| ↑ / ↓ | Percorrer campos e linhas de botões, parando nas extremidades |
| Enter | Confirmar login/cadastro ou o botão focado; sair da conta quando autenticado |
| Ctrl+N | Alternar login/cadastro antes de autenticar |
| Ctrl+R | Renovar a sessão; reconectar quando a conexão de tempo real foi interrompida |
| ← / → | Mover o cursor no campo; alternar entre os botões da mesma linha |
| Home / End | Mover o cursor para o início/fim do campo |
| Backspace / Delete | Apagar texto |
| Clique esquerdo no campo | Focar e posicionar o cursor |
| Clique esquerdo no botão | Entrar, sair da conta ou selecionar perfil |
| Ctrl+U | Limpar o campo atual |
| Ctrl+P | Selecionar perfil; encerrar a conexão e limpar credenciais locais |
| Esc | Cancelar uma conexão em andamento |
| Ctrl+Q | Sair do aplicativo |
| Ctrl+C | Limpar o campo focado; sair quando o foco está fora dos campos |

No login e no cadastro, os botões de ação ficam na mesma linha: **Entrar / Cadastro** ou **Criar conta / Login**. **Selecionar perfil** fica no canto superior direito do formulário. ↓ na senha do servidor foca a ação principal; ←/→ alternam os dois botões, ↓ foca Selecionar perfil e ↑ volta à senha do servidor. ↑ em Selecionar perfil volta à ação principal, e ↓ mantém o foco nesse botão. As setas não dão a volta na tela. Tab segue campos, ação principal, ação secundária e Selecionar perfil; controles indisponíveis são pulados.

Backspace no início do campo mantém o foco; segurar a tecla não navega entre inputs. Em terminais com suporte a mouse, clique no campo para editar naquele ponto ou no rótulo para editar ao final. Cliques fora dos controles são ignorados; durante a conexão, os controles ficam bloqueados.

Colagem delimitada pelo terminal não envia com quebras de linha. Redimensionar preserva os dados; em janela menor que o mínimo, o aplicativo solicita ampliação. Erros de autenticação e rede são apresentados na tela.

## Objetivo

A primeira versão, v0.1.0, permitirá cadastrar várias instâncias, entrar em uma delas e conversar por canais de texto e mensagens diretas, com histórico, tempo real e reconexão. O login é solicitado quando a última sessão salva não puder ser recuperada e validada. Cada instância mantém suas próprias contas, conversas e sessão. A API atual não especifica federação entre servidores.

Consulte [instalação e uso](docs/uso.md) para os requisitos, execução, conexão e solução de problemas.

## Stack e estrutura

- C17 e CMake.
- ncursesw para renderização, navegação e painéis; formw para os campos de login.
- libunibreak para delimitar grafemas no editor de mensagens e na apresentação de texto.
- libcurl multi para HTTP e handshake WS assíncronos, TLS, URLs e cookies; APIs WS para leitura/escrita sem bloquear a interface.
- Jansson para JSON.
- libsixel para saída Sixel e libchafa para os protocolos gráficos Kitty/iTerm2; FFmpeg para imagens e libmpv para vídeo/áudio. Um pool de dois workers da GLib processa imagens; um worker separado cuida do vídeo. O terminal e a rede permanecem na thread principal.

```text
src/main.c       Entrada e ciclo de vida
src/app/         Conexões abertas por instância
src/ui/          Login, perfis, leitura, menções, tema e edição Unicode
src/model/       Canais, histórico paginado e autores
src/config/      Perfis públicos e persistência XDG
src/net/         Estado de autenticação e transporte HTTP
tests/           Backend simulado e testes em pseudoterminal
docs/            Instalação e uso do programa
openapi.yml      Contrato do backend
```

## API

Use [`openapi.yml`](openapi.yml) como referência para endpoints, autenticação, modelos e eventos WebSocket. Confirme o contrato antes de implementar qualquer comportamento do cliente; não presuma que a API oferece recursos que não estão descritos.

As ferramentas em `tools/` auxiliam a validação do contrato durante o desenvolvimento. Seus relatórios e inventários são gerados em `build/`; não fazem parte da documentação de uso.

## Estado atual

Cards de embed, imagens por Sixel/Kitty/iTerm2 e player de vídeo em modal com áudio mantêm
a interface em ncurses. Dependências adicionais:
libsixel (MIT), [libchafa](https://hpjansson.org/chafa/development/) (LGPLv3+, protocolos Kitty/iTerm2), FFmpeg (decodificação de imagens) e libmpv (reprodução e áudio).
Os downloads continuam no transporte autenticado do Papo; a mídia não recebe
cookies nem URLs remotas no player. O backend atual não expõe os bytes da imagem
adicional do embed nem do ícone do rodapé.

- [x] Especificação OpenAPI disponível.
- [x] Tela de login, HTTP e aplicativo compilável.
- [x] Testes com backend simulado e pseudoterminal.
- [x] Inventário e fixtures de contrato com validação OpenAPI 3.0.
- [x] Login público, sessão e handshake WS nativo observados na instância local.
- [x] R0: contrato e ambiente de integração validados com a conta fornecida, paginação e cenário privado HTTPS/WSS.
- [x] Transporte WS/WSS em C, com cookies por contexto, filas limitadas e revalidação REST.
- [x] Base de terminal exercitada em Kitty, Konsole e tmux.
- [x] Interface de conversas: editor multilinha, teclado, mouse e histórico.
- [x] R3: perfis XDG, descoberta por URL, isolamento de contextos e preferências de taxa/burst, com validação automatizada e demonstração no backend real.
- [x] R4: cadastro, login público/privado, renovação serializada, revogação e logout, com identidade REST/WS e cookies isolados por instância.
- [x] R5: canais ordenados, tópicos, histórico paginado, autores e metadados em modo de leitura.
- [x] R6: envio multipart, rascunhos por sessão/canal, confirmação no histórico e retry manual com resultado incerto.
- [x] R7: eventos em tempo real, reconciliação REST/WS, reconexão automática e recuperação da janela carregada.

## Conversar em canais

Após entrar, a lista de canais abre automaticamente. F6 volta à tela da conta; o botão **Canais / F6** e a tecla F6 permitem reabrir a leitura. Escolha um canal com ↑/↓ e abra com Enter, → ou clique. Navegar pela lista mantém o histórico e o recebimento de mensagens em tempo real do canal aberto; confirmar ou clicar no canal atual preserva mensagens, posição de leitura e rascunho, sem recarregar. Clique no título **Canais** para focar a lista. Categorias são separadores; canais de voz mostram que chamadas ainda não estão disponíveis. A leitura usa apenas o canal aberto, e consultar seu histórico também marca leitura no servidor. `*` indica uma última mensagem ainda não lida nos dados consultados.

A tela organiza abas de servidores no topo, com linha separadora e uma linha de margem abaixo, canais à esquerda, histórico e área de edição no painel central. **+ Adicionar**, à direita das abas, abre o formulário de login/cadastro de outro servidor. Depois do login, a nova conexão aparece em uma aba; clique nas abas para trocar de servidor. As sessões ficam separadas, até o limite de 64 conexões nesta execução; somente a última conexão ativa pode ser recuperada na próxima abertura. Entrar novamente na mesma URL substitui sua conexão anterior. Esc no formulário de adição volta ao servidor anterior; se houver uma conexão em andamento, Esc primeiro a cancela.

No histórico, ↑/↓ e PageUp/PageDown rolam o texto. A lista de canais permanece estável durante a rolagem. Home vai ao início da janela carregada; pressione PageUp nesse ponto para buscar a página anterior sem deslocar a mensagem que está lendo. End consulta novamente a página recente. Ctrl+R antecipa a reconexão quando desconectado; conectado, atualiza o canal aberto ou repete a consulta que falhou; uma falha ao buscar mensagens antigas mantém a janela atual para nova tentativa.

Ao abrir a leitura, o foco começa nas abas. Use ←/→ para escolher o servidor ou destacar **+ Adicionar**, e Enter para abrir o formulário. O botão fica branco sem foco e laranja ao selecionar, quando aparece como **>+ Adicionar<**. ↓ entra na lista de canais.

Em tela ampla, Tab/Shift+Tab alternam entre abas, canais, histórico e editor quando há um canal de texto aberto. ← nos canais, ou ↑ no primeiro canal, volta às abas; ←/→ nas abas trocam de servidor e Enter em **+ Adicionar** abre o formulário. ↓ nas abas foca os canais. Esc sai do editor para o histórico; nos demais painéis, alterna canais/histórico. Abaixo de 80 colunas, Tab/Shift+Tab e os controles **Canais / Conversa** alternam o painel visível. F6 volta à conta, Ctrl+L sai da conta e remove sua aba, Ctrl+P abre a seleção de perfil encerrando a sessão local atual, e Ctrl+Q fecha o programa; Ctrl+C limpa o campo focado ou fecha fora dos campos. O mínimo dessa tela é 44×14; para abrir o formulário, amplie para 44×22.

Mensagens aparecem em ordem cronológica, com data, autor, indicação de edição e referências de resposta. Anexos mostram seus metadados e, em terminais com protocolo gráfico compatível, imagens visíveis são baixadas e renderizadas no histórico. Embeds aparecem em cards com autor, título, descrição, fields e rodapé. F7 ou clique abre a mídia em um modal; vídeos são baixados antes de reproduzir com áudio. Os embeds recebidos ou atualizados em tempo real aparecem na mensagem sem precisar reabrir o canal, incluindo mensagens sem texto. Autores ausentes têm um rótulo de indisponibilidade. Ao abrir um canal de texto, digite para focar o editor, incluindo o primeiro caractere. Enter no histórico também foca o editor. Enter no editor envia, Ctrl+N insere uma nova linha e colagem delimitada preserva linhas sem enviar. Setas, Home/End, Backspace e Delete editam grafemas; clique no editor para posicionar o cursor. Esc volta ao histórico. A área em foco recebe um fundo discretamente mais claro. Mensagens de outras contas chegam automaticamente. Eventos repetidos e confirmações REST/WS da mesma mensagem são reconciliados pelo ID. A tela acompanha novas mensagens quando você está no fim; ao ler mensagens anteriores, indica novidades sem mover a leitura. End ou clique no indicador volta ao fim.

A janela em memória contém até 1.000 mensagens ou 4 MiB de JSON serializado. Ao carregar páginas antigas além desse limite, libera mensagens recentes; End volta a consultá-las. Trocar de canal ou servidor descarta seu histórico local. As outras abas e os históricos não são persistidos; somente o cookie da última sessão pode ficar no chaveiro. Abrir a lista de canais não consulta seus históricos em segundo plano.

Rascunhos e cursor ficam em memória por sessão e canal, inclusive ao trocar de servidor ou voltar à conta com F6. Você pode continuar digitando durante o envio; a confirmação remove somente o texto enviado que permaneceu intacto. Texto reescrito e continuação são preservados. Sair da conta, substituir a conexão da mesma URL ou fechar o programa descarta esses dados. O editor aceita até 2048 caracteres Unicode/8192 bytes UTF-8; cada sessão guarda até 64 rascunhos e 32 operações.

O editor informa quando o envio está aguardando, enviando, confirmado, rejeitado ou sem confirmação. Falhas preservam o texto. **Ctrl+T** tenta novamente o texto do último envio com falha no canal atual. Timeout ou resposta perdida podem ocorrer após a criação da mensagem; nesse caso, Enter ou Ctrl+T abre uma confirmação, inicialmente em **Cancelar**. Se desejar reenviar mesmo com possibilidade de duplicação, escolha **Reenviar** com → ou clique e confirme. Não há reenvio automático.

Após uma queda, a aba ativa revalida a sessão, atualiza canais e recupera mensagens novas e a janela de histórico carregada, incluindo edições e exclusões ocorridas offline. Rascunhos e posição de leitura são preservados enquanto suas mensagens ainda estiverem disponíveis. Os limites de memória continuam valendo; se a mensagem usada como referência desaparecer, a leitura usa a próxima sobrevivente, ou a anterior. A recuperação não oferece replay completo: alterações fora da janela são consultadas ao abrir novamente esse trecho. Nenhum envio pendente é repetido automaticamente.

A última linha acompanha o foco: mostra à esquerda somente as dicas de abas,
canais, histórico, editor ou do cartão/modal aberto. À direita ficam os avisos,
separados por `|`, com o mais recente no canto direito. Conexão, histórico,
envio, edição, eventos e mensagens não lidas têm avisos independentes; uma
confirmação de envio não apaga o estado da conexão. Um novo aviso substitui
apenas o anterior do mesmo contexto. Em janelas estreitas, os textos ficam
mais curtos; `+N` indica avisos antigos que reaparecem ao ampliar a janela.
Trocar de canal limpa os avisos da conversa anterior e preserva a conexão.

O campo da conversa mostra **Conversar em #nome_canal** quando está vazio e
cresce de uma até cinco linhas conforme o texto quebra ou ocupa mais espaço.
Depois disso, as setas acompanham o cursor e a roda do mouse sobre o campo
rola o texto; as marcas ↑/↓ indicam conteúdo fora da área visível. Em telas
baixas, o campo usa as linhas disponíveis preservando a leitura e o rodapé.
O campo tem divisórias acima e abaixo, com uma linha vazia de espaço entre o
texto e cada divisória. Em telas muito baixas, esse espaço é reduzido. O
indicador de digitação fica abaixo da divisória inferior.

Em todos os campos editáveis, **Shift+setas** estende a seleção; clique esquerdo
e arraste também selecionam, com realce durante o movimento, inclusive entre
linhas e em linhas quebradas pela largura do campo. A seleção tem fundo e texto contrastantes; digitar,
apagar ou colar substitui o trecho selecionado. **Ctrl+←** vai ao começo da
palavra; **Ctrl+→** vai ao final, antes dos espaços. A partir de um espaço,
Ctrl+→ avança até o fim da próxima palavra.
**Home/End** vai ao início/fim da linha, e **Ctrl+C** ou **Ctrl+U** limpa o campo.
**Ctrl+Q** sempre fecha o aplicativo. **Ctrl+Shift+C** ou clique direito sobre
um campo com seleção copia o texto; **Texto copiado** aparece por três segundos, preservando os
outros avisos. A cópia usa `wl-copy` no Wayland ou `xclip` no X11 quando
estão disponíveis; caso contrário, usa o terminal via
[OSC 52](https://sw.kovidgoyal.net/kitty/clipboard/), que precisa permitir a cópia.
As senhas permanecem mascaradas, inclusive durante seleção.

Ctrl+Shift+C precisa chegar ao aplicativo. Terminais que reservam esse atalho
para sua própria seleção precisam configurá-lo para enviar `ESC[99;6u`;
clique direito também copia a seleção do campo. O cliente aceita os atalhos
encaminhados por CSI-u e modifyOtherKeys, incluindo Num Lock/Caps Lock, e mantém
o modo de teclado do terminal. Ctrl+C continua limpando o campo; em terminais
que enviam o mesmo código para os dois atalhos, use o clique direito para copiar.

No Kitty, adicione esta [regra condicional](https://sw.kovidgoyal.net/kitty/mapping/#conditional-mappings)
ao `~/.config/kitty/kitty.conf` para copiar a seleção do Papo TUI com Ctrl+Shift+C:

```conf
map --when-focus-on=var:papo_tui=1 ctrl+shift+c send_text all \x1b[99;6u
map --when-focus-on=var:papo_tui=1 ctrl+v paste_from_clipboard
```

Recarregue a configuração do Kitty com **Ctrl+Shift+F5** e reinicie o Papo TUI.
O aplicativo define o marcador ao abrir e o remove ao sair. Nos outros
programas, o atalho continua usando a cópia do terminal.
A segunda regra permite **Ctrl+V** para colar em bloco dentro do Papo TUI.

No Kitty e no Konsole em modo de 256 cores, o tema usa tons aproximados que
permanecem estáveis ao recarregar a configuração do terminal. Terminais com
entrada de cores RGB diretas usam os tons exatos.

A colagem delimitada pelo terminal entra em bloco ao terminar, inclusive ao
substituir uma seleção, e não aciona comandos nem envia quebras de linha.
No login e nos campos de perfil, quebras de linha são descartadas. Uma colagem
que excede o limite mantém o campo original e mostra o aviso correspondente.
O atalho de colar continua sendo o configurado no terminal.

Na linha abaixo do campo de texto, uma pessoa aparece como “Nome está digitando…” ou
com sua frase personalizada de digitação cadastrada no servidor. De duas a
quatro pessoas, aparecem os nomes separados por vírgula e “estão digitando…”;
acima de quatro, “Várias pessoas estão digitando…”. O indicador acompanha
somente o canal aberto, não inclui você e desaparece após seis segundos sem
atividade, quando a pessoa envia uma mensagem ou fica offline. Ao editar,
o aplicativo avisa pelo WebSocket no máximo uma vez a cada dois segundos.

## Menções

Digite `@` no editor para abrir o cartão de usuários e continue digitando para
filtrar por nickname ou username. A busca ignora espaços e símbolos: `@eana`
encontra, por exemplo, `EA -* NA`, preservando o nome original. ↑/↓ e
PageUp/PageDown navegam; Tab ou Enter insere o usuário destacado. A roda do
mouse sobre o cartão rola a lista e o
clique esquerdo seleciona. Esc fecha sem alterar o texto. Enter com o cartão
aberto escolhe a menção; depois de fechá-lo, Enter envia a mensagem.

O editor mostra o nome, mantendo a identidade do usuário no envio. Uma menção
confirmada é uma unidade: setas a atravessam e Backspace/Delete a removem por
inteiro. Todas as menções aparecem em laranja, inclusive nos títulos,
descrições, autor, rodapé e campos dos embeds, independentemente da cor do
card. Mensagens que mencionam sua conta, também nos embeds, recebem uma
barra vertical à esquerda. Sem cores, o destaque usa negrito.
As mensagens continuam chegando pelo WebSocket enquanto o cartão está aberto.

O cartão mostra primeiro quem enviou mensagens mais recentemente no canal
aberto, considerando o histórico já carregado. Os demais ficam em ordem
alfabética. A ordem acompanha as novas mensagens, mantendo o usuário destacado
para que uma atualização não altere sua escolha.

A lista reúne usuários da instância e é carregada em páginas, sem consulta de
históricos inativos. Durante o carregamento, os resultados são parciais. O
cache fica somente em memória, limitado a 10.000 usuários ou 16 MiB, incluindo os índices de pesquisa. Usuários sem referência no histórico, na digitação ou no seletor expiram após cinco minutos; abrir novamente o seletor recarrega as páginas necessárias. Se houver
falha, Ctrl+R no cartão tenta novamente. Uma única opção também exige
confirmação; textos colados, e-mails e URLs não abrem o seletor automaticamente.

## Imagens, vídeos e embeds

F7 no histórico abre a mídia visível; quando há vários itens, ↑/↓ e Enter
escolhem um deles. Clique numa prévia também abre o modal. Esc fecha ou cancela
o carregamento. No vídeo, Espaço pausa, ←/→ avançam ou recuam 5 segundos, ↑/↓
alteram o volume e `m` silencia. Redimensionar preserva a reprodução; rascunhos
e leitura permanecem ao voltar. O áudio usa o dispositivo disponível; sem um
dispositivo, o vídeo continua e o modal informa a indisponibilidade do áudio.

O modal centraliza a mídia e usa margens que preservam a coluna de canais em
telas largas. Esc ou clique fora fecha a visualização. Clique no botão
**[Info]**, fixo no cabeçalho do modal, para abrir ou fechar as propriedades
à direita: nome, MIME, bytes, moderação,
dimensões e identificadores disponíveis. PageUp/PageDown ou a roda do mouse
sobre esse painel rola os dados; `i` e F8 também alternam o painel. Nenhum novo
download é necessário para abri-lo.

Imagens JPEG, PNG, WebP e GIF são exibidas por Sixel ou pelos protocolos gráficos Kitty/iTerm2.
Anexos de imagem mostram apenas a mídia, usando o arquivo original sem ampliar
suas dimensões; imagens maiores são reduzidas proporcionalmente para a largura
disponível e o limite de 640×360 pixels. GIFs animados se repetem no histórico
e no modal, com até 256 frames/32 MiB de pixels decodificados por arquivo.
A negociação consulta capacidades e tamanho das células
sem bloquear a interface ou inserir respostas no editor. Sem suporte gráfico
confirmado, os cards e metadados continuam em texto e nenhuma imagem é baixada
automaticamente. URLs externas permanecem texto.

As imagens são carregadas somente no trecho visível da conversa aberta. Vídeos
MP4, WebM e Ogg carregam por completo antes do player, sem autoplay. O limite é
16 MiB/16 megapixels por imagem e 128 MiB por vídeo. O cache de imagens em
memória tem orçamento global de **256 MiB**, compartilhado pelas sessões desta
execução, incluindo frames e variantes gráficas; a saída gráfica do player é limitada a 640×360 pixels e 15 frames/s. A qualidade
final também depende do terminal. Mídia inválida, codec ausente, falta de
permissão e limites aparecem na tela; F7/Enter no erro permite tentar novamente.

Os arquivos ficam em diretório temporário privado (`700`), com arquivos `600`,
sem cookies. Imagens recentes podem ser reutilizadas ao trocar de canal/servidor
ou voltar à conta, dentro da mesma sessão; sair da conta limpa seu contexto.
Há até quatro downloads simultâneos, com prioridade para o modal. Imagens com
os mesmos bytes compartilham o objeto e suas variantes dentro da sessão;
instâncias e contas permanecem isoladas. As representações fora da tela expiram
após um minuto, e os arquivos sem uso, após cinco minutos. Os arquivos de imagem
concluídos têm limite separado de 128 MiB. Sob pressão, o cache libera as
representações menos recentes e GIFs podem passar ao primeiro frame. Itens
adiados por falta de orçamento voltam ao processamento quando há espaço.
Fechar o modal cancela seu carregamento e remove o vídeo temporário. O orçamento
de imagens cobre também os buffers gráficos reservados ao player; ele não é um
limite absoluto para o RSS do processo.
Requisições usam os cookies da própria instância, a verificação TLS e o mesmo
limite global de requisições do restante do cliente. HTTP 429 exige nova
tentativa manual após a espera.

Cards usam uma barra à esquerda na cor do embed e fundo discretamente mais
claro, sem borda ao redor nem rótulos de embed/rodapé. Autor e rodapé têm
menos destaque; o tamanho da fonte é definido pelo terminal. Mensagens sem
texto não recebem um texto substituto. Fields ficam em até três colunas,
ou empilhados em tela estreita. O avatar fica junto ao autor. A miniatura
de embeds customizados fica à direita quando há espaço; prévias de links e
miniaturas em telas estreitas ficam abaixo dos fields. Ambos usam os bytes
expostos pelo backend. A imagem adicional indica indisponibilidade; seus
bytes e os do ícone do rodapé não são expostos pela API atual. Vídeos de
iframe, como YouTube, mostram URL e metadados. O editor continua enviando texto; não há interpretação de Markdown
do Discord nem execução de HTML remoto.

## Recuperar a última sessão

Ao abrir normalmente, o programa tenta recuperar apenas a última conexão ativa. O cookie fica no GNOME Keyring/Secret Service ou KWallet 5/6, separado das senhas e dos arquivos de configuração. A conta é confirmada por REST antes de abrir seus canais; as outras abas não são restauradas.

O suporte usa bibliotecas opcionais: `libsecret-1` 0.20+ e `gio-2.0` 2.66+. No Arch, instale `libsecret` e `glib2` antes de configurar o CMake. Seus testes usam `dbus-run-session` e `gdbus` com um chaveiro simulado em um barramento privado. Sem gerenciador disponível ou quando o desbloqueio for recusado, a sessão fica somente em memória; a próxima abertura mostra login com a última URL preenchida. `--profiles` abre a seleção explicitamente. Para compilar sem adaptadores de chaveiro, use `-DPAPO_DESKTOP_SESSIONS=OFF`.

A renovação automática permanece ativa. Um JWT de 24 horas pode ser renovado pelo backend com o cookie ainda válido; cada rotação emite um novo JWT de 24 horas e cookie de 30 dias. A validação REST é a fonte de verdade. Fechar o programa mantém a última sessão salva; **Sair da conta** invalida sua recuperação e solicita revogação. Erros de rede na recuperação mostram o login; credenciais definitivamente rejeitadas são removidas.

Somente dados públicos e um marcador de recuperação ficam em `$XDG_STATE_HOME/papo-tui`, ou `~/.local/state/papo-tui`, com diretório `700` e arquivos `600`. Builds sem GIO guardam esses dados públicos junto à configuração. A configuração local define um contexto independente, e uma segunda janela nesse contexto exige login próprio, sem compartilhar a sessão persistida. Cookies nunca são gravados em arquivos de texto.

## Testes

```sh
python -m venv build/test-env
build/test-env/bin/python -m pip install -r requirements-dev.txt
cmake -S . -B build -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
```

Os testes usam credenciais sintéticas e servidores HTTP/WS locais. Cobrem acesso público/privado, cookies, logout, erros, limites de entrada/resposta/fila, cancelamento, URLs com prefixo, senhas ocultas, espaços em senhas, redimensionamento, Backspace repetido, cliques e restauração do terminal. pyte reconstrói a tela para verificar a posição do cursor. A suíte WS inclui fragmentação dentro de UTF-8, chunks, ping/pong intercalado, encerramento, timeouts, sessão revogada com ACK e WSS com CA/hostname verificados. Essas dependências Python são usadas somente nos testes; os mocks não comprovam integração com a instância real.

A suíte `unicode-editor` valida grafemas, largura e cursor. Os testes de interação usam backends simulados para validar os controles dos canais reais, incluindo menções, histórico, envio e mídia.

As suítes `instance-profiles` e `profiles-terminal` verificam cadastro, IDs estáveis, remoção confirmada, reinício, migração da configuração, permissões, arquivos inválidos, links, concorrência, URLs, descoberta pública/privada, DNS, timeout, TLS, redirecionamento, cookies por contexto e descarte de respostas atrasadas. Cobrem seleção pelo login, navegação, cores, terminal sem cores, senha do servidor com foco/erro, limites configuráveis e resposta da interface durante a espera. `request-rate-limit` verifica também taxa fracionária, mudança de burst e preservação de cooldown.

Para demonstrar o R3 contra uma instância já em execução, sem autenticar nem cadastrar contas:

```sh
build/test-env/bin/python -m tools.check_r3 --url http://127.0.0.1:8080
```

O probe usa configuração temporária, cadastra dois perfis locais, corrige uma URL inválida, renomeia e reinicia o cliente. Registra resultados e telas em `build/r3/`. `--backend-binary /caminho/papo-server` permite usar um executável real com uma cópia temporária de esquema e metadados do servidor no container PostgreSQL `papo_postgres`; nenhum dado do banco de origem é alterado, e o banco/processo temporários são removidos ao terminar. `--postgres-container` escolhe outro container. `--tls-proxy build/integration/tls-proxy` inclui descoberta HTTPS e rejeição de CA/hostname inválidos usando o proxy Go de desenvolvimento.

As suítes `session-expiry`, `session-integration` e `session-terminal` verificam cadastro sem login automático, política/foco de campos, autorização privada expirada, renovação serializada, cookie atualizado, recuperação única de 401, banimento, logout durante refresh, falha remota de logout, prazos curtos, orçamento global e controles de teclado/mouse com senhas mascaradas.

Para demonstrar R4 com o backend real em um banco temporário, use o executável Go do serviço e o container PostgreSQL de desenvolvimento. O proxy de desenvolvimento requer Go:

```sh
mkdir -p build/integration
go build -o build/integration/tls-proxy tools/tls_proxy.go
build/test-env/bin/python -m tools.check_r4 --backend-binary /caminho/papo-server --tls-proxy build/integration/tls-proxy
```

Essa ferramenta cria contas sintéticas somente na cópia temporária, exercita cadastro/login público e privado, REST/WSS, renovação, logout, revogação, banimento, confirmação de senha no cadastro e reinício da TUI. O banco de origem é apenas lido; contas, banco, processos, configuração e chaves TLS temporários são removidos ao terminar. `--postgres-container` escolhe outro container. O relatório contém verificações sem credenciais em `build/r4/validation/summary.json`.

As suítes `history-integration` e `history-terminal` verificam paginação com datas iguais, merge por ID, nomes de autores, campos nulos, erros, descarte de respostas atrasadas, isolamento de instâncias, limite de memória, âncora de leitura, redimensionamento e sanitização de texto remoto.

Para demonstrar R5 com o backend real numa cópia temporária do banco:

```sh
go build -o build/integration/tls-proxy tools/tls_proxy.go
build/test-env/bin/python -m tools.check_r5 --backend-binary /caminho/papo-server --tls-proxy build/integration/tls-proxy
```

A ferramenta cria contas e 250 mensagens somente no banco temporário, testa três páginas com timestamps iguais, retorno ao recente, nomes de autores, permissões, leitura remota e a TUI via HTTPS. O proxy aplica um atraso controlado de 600 ms às respostas de histórico para exercitar troca de canal durante uma consulta. O banco de origem é apenas lido; contas, mensagens, banco, processos e chaves temporários são removidos ao terminar. Evidências sem credenciais ficam em `build/r5/validation/backend.json`.

R6 integra transporte multipart e editor à tela real de conversas. As suítes `message-send-integration`, `message-send-terminal` e `message-send-product` verificam o transporte, o componente e o aplicativo completo: confirmação no histórico, edição durante envio, rascunhos/cursor entre canais e servidores, retorno à conta, Unicode, cancelamento, 401 sem replay e confirmação manual após resposta perdida.

Para validar R6 com duas contas sintéticas em uma cópia temporária do backend:

```sh
mkdir -p build/integration
go build -o build/integration/tls-proxy tools/tls_proxy.go
build/test-env/bin/python -m tools.check_r6 --backend-binary /caminho/papo-server --tls-proxy build/integration/tls-proxy
```

A ferramenta cria somente usuários/canais/mensagens temporários, confirma uma conversa A→B/B→A pela tela real, edição na PTY, perda de confirmação e timeout após criação. O POST incerto exige tentativa manual com indicação de possível duplicação. O banco de origem é apenas lido; recursos temporários são removidos ao terminar. Os resultados sem credenciais ficam em `build/r6/validation/summary.json`. O observador `send-terminal-probe` pertence às ferramentas de desenvolvimento e não substitui a tela de conversas do programa.

R7 acrescenta as suítes `realtime-integration`, `realtime-terminal` e `websocket-partial-output`, cobrindo concorrência REST/WS, eventos parciais, exclusões, reconexão, leitura preservada e escritas parciais. Para validar duas contas na tela real com HTTPS/WSS e banco temporário:

```sh
go build -o build/integration/tls-proxy tools/tls_proxy.go
build/test-env/bin/python -m tools.check_r7 --backend-binary /caminho/papo-server --tls-proxy build/integration/tls-proxy
```

O roteiro simula indisponibilidade no proxy de desenvolvimento e produz mensagens, edições e exclusões enquanto os clientes estão desconectados. Recursos temporários são removidos ao terminar; evidências sem credenciais ficam em `build/r7/validation/summary.json`.

As suítes `media-cache`, `media-layout-sixel`, `media-integration` e `media-terminal` verificam
cards, fields inline, sanitização, pixels Sixel/Kitty/iTerm2, imagens JPEG/PNG/WebP/GIF,
player com áudio, pausa/seek/resize, arquivos privados, cancelamento e falhas
HTTP e remoção de imagens ao trocar de canal ou fechar o modal. A PTY responde às consultas gráficas de forma sintética; essa evidência é
separada das capturas de emuladores reais.

`media-cache` usa barreiras HTTP, falhas de alocação e relógio controlado para
verificar concorrência, identidade compartilhada, fila, cancelamento e pressão
sobre 256 MiB. No Linux, para comparar carregamento, codificações, latência de teclado e
pico de RSS com um binário anterior, após compilar com testes habilitados:

```sh
build/test-env/bin/python -m tools.check_media_performance --baseline /caminho/papo-tui-anterior
```

As métricas ficam em `build/performance/comparison.json`. Esse cenário usa
backend simulado e um respondedor Kitty em PTY; as capturas em emulador real e a
integração com o backend são verificações separadas.

Para validar os downloads HTTPS e a apresentação contra o backend real em
banco temporário:

```sh
go build -o build/integration/tls-proxy tools/tls_proxy.go
build/test-env/bin/python -m tools.check_media --backend-binary /caminho/papo-server --tls-proxy build/integration/tls-proxy
```

A ferramenta cria somente dados sintéticos na cópia temporária do banco e
remove contas, arquivos e processos ao terminar. Evidências sem credenciais
ficam em `build/media/validation/summary.json`. Para capturas de janelas
dedicadas de Konsole e Kitty em sessão X11/XWayland, com qdbus6, xdotool e
ImageMagick disponíveis:

```sh
build/test-env/bin/python -m tools.check_media_terminals
```

Capturas e resultados ficam em `build/media/terminals/`; esse roteiro usa
backend simulado e não abre o chaveiro pessoal.

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

Os resultados e as telas ficam em `build/r1/terminals/`. A ferramenta usa uma configuração temporária de Konsole e um servidor tmux isolado; fecha suas janelas ao terminar. Eventos de mouse são enviados à PTY no protocolo declarado pelo terminfo; `infocmp` também deve estar disponível.

```sh
cmake -S . -B build/sanitized -DPAPO_SANITIZERS=ON -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build/sanitized
ctest --test-dir build/sanitized --output-on-failure
```

Em ambientes sob ptrace, o LeakSanitizer não funciona. Nesses ambientes, execute `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitized --output-on-failure`: AddressSanitizer e UndefinedBehaviorSanitizer continuam ativos, mas vazamentos não são verificados.
