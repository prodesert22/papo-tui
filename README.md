# Papo TUI

Cliente de conversas para terminal, em C, para conectar-se a instâncias independentes do Papo por URL.

O aplicativo oferece login, cadastro de conta, renovação de sessão, perfis locais com nome e URL e leitura de canais com histórico paginado. Login e cadastro usam URL do servidor, usuário, senha e senha do servidor. A tela organiza cabeçalho, formulário e estado/controles em três áreas. O aplicativo consulta a instância, autoriza o acesso privado quando necessário, autentica a conta e confirma a sessão. As senhas são ocultas e os cookies ficam somente em memória.

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

Há exemplos de canal vazio, carregamento e erro recuperável: na primeira instância, **ajuda**, **ideias** e **eventos**, respectivamente. No erro, Ctrl+R, Enter com foco no histórico ou clique em **Tentar novamente** repetem o carregamento simulado. Ctrl+Q abre uma confirmação quando há rascunhos; Ctrl+C fecha diretamente. Os dados são descartados ao sair. O cadastro de perfis reais fica no login; a leitura de canais reais fica disponível após o login; o envio e a atualização automática de mensagens permanecem nas próximas entregas.

Use um terminal UTF-8, com pelo menos 44 colunas e 22 linhas disponíveis ao aplicativo. No tmux, reserve também as linhas da barra de status. Para compilar somente o aplicativo, use `-DBUILD_TESTING=OFF` na configuração do CMake. `--help` e `--version` funcionam fora de um terminal interativo.

## Login

### Perfis de instância

Ao iniciar normalmente, o aplicativo sempre abre a tela de login. O botão **Selecionar perfil**, disponível por clique ou Tab e Enter, abre um modal com os perfis salvos; **Ctrl+P** é o atalho para esse modal. Use ↑/↓ e Enter, ou clique no perfil, para preencher o login. No modal, **Gerenciar / Ctrl+P** abre o cadastro e **Cancelar / Esc** volta ao login.

Para iniciar diretamente no gerenciador completo:

```sh
./build/papo-tui --profiles
```

O gerenciador tem duas opções no topo: **Listar** e **+ Adicionar**. Em **Listar**, os perfis aparecem numa lista vertical com rolagem automática: ↑/↓ mudam o destaque, PageUp/PageDown avançam uma página e Home/End vão ao início/fim. Enter ou → na lista levam ao primeiro botão do rodapé, **Selecionar**, seguido de **Editar**, **Remover**, **Limites** e **Identificar**. Confirme **Selecionar** para preencher o login. Nas ações, ←/→ movem entre os botões e param nas extremidades; ↑ volta à lista. ↑ no primeiro perfil volta ao topo; ali, ← vai para **Listar**, → para **Adicionar**, e ↓ ou Enter em **Listar** entram na lista. ← na lista mantém o foco. Tab/Shift+Tab também percorrem as áreas e botões. Clicar num perfil destaca-o; clicar num botão executa sua ação.

No gerenciador e nos modais de perfil, os textos principais e botões são brancos; o texto dos botões e itens de lista selecionados usa laranja `#ffb347`, e dicas e comandos permanecem cinza. A área em foco usa um fundo laranja escuro mais evidente, enquanto o perfil destacado fora do foco mantém um fundo laranja discreto. As dicas de navegação do rodapé acompanham a área focada.

Em terminais sem cores, o foco usa inversão e o perfil destacado fora do foco usa negrito. Em telas estreitas, o rodapé encurta os atalhos para manter os controles visíveis.

**+ Adicionar** abre o cadastro de nome e URL por clique ou Enter. Enquanto essa opção estiver selecionada, as ações do rodapé ficam indisponíveis, inclusive depois de cancelar o cadastro. Salvar um perfil volta à lista com ele destacado. **Editar** renomeia ou corrige a URL mantendo o ID local; **Remover** exige confirmação e começa em **Cancelar**. **Ctrl+N**, **Ctrl+E** e **Ctrl+D** abrem essas três ações. Esc cancela o diálogo ou volta ao login, e Ctrl+Q/Ctrl+C fecha o aplicativo. Colagem delimitada não confirma ações automaticamente.

Ao abrir a seleção de perfil pelo login, a conexão de tempo real é encerrada e cookies, usuário e senhas locais são descartados. Selecionar outro perfil exige novo login. A URL do perfil fica bloqueada no formulário; altere-a em **Selecionar perfil → Gerenciar → Editar**. Os perfis não contêm mensagens nem rascunhos da demonstração.

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

Depois de autenticar, a lista de canais abre automaticamente. Escolha um canal para ler suas mensagens. O aplicativo abre um WebSocket autenticado com os cookies dessa sessão e recebe eventos em memória. F6 volta à tela da conta, onde você pode renovar ou sair da sessão; **Canais / F6** reabre a leitura. Login e senha não são persistidos pelo aplicativo.

**Cadastro / Ctrl+N** alterna entre login e criação de conta, preservando URL e usuário e limpando as senhas. O cadastro exige usuário com pelo menos 3 caracteres e senha com pelo menos 8 caracteres, uma letra maiúscula e um caractere especial; a instância pode impor limites adicionais. Repita a senha em **Confirmar senha**. Senhas diferentes ou confirmação vazia bloqueiam o envio e focam a confirmação. Senha curta, sem maiúscula ou sem caractere especial recebe uma mensagem específica e foco para correção. Em uma instância privada, informe também a senha do servidor. Depois de criar a conta, digite novamente a senha e confirme **Entrar**: cadastro não faz login automático. Usuário já existente recebe foco para correção; falhas de política exibem uma mensagem sem revelar detalhes remotos.

A sessão é revalidada por REST a cada 30 segundos; heartbeat/ACK do WebSocket não comprova autenticação. A primeira renovação ocorre até 30 segundos após o login e as seguintes são agendadas antes da expiração informada pelo servidor. **Renovar / Ctrl+R** antecipa a renovação. Falhas de autenticação, revogação, banimento ou identidade incompatível limpam a sessão e devolvem o foco à senha para novo login.

Falhas de tempo real aparecem na área de estado. Nessa situação, **Reconectar / Ctrl+R** refaz a conexão sem repetir o login, quando a sessão REST continua válida. **Sair da conta** encerra a sessão local mesmo se a revogação remota falhar; a tela informa quando ela não foi confirmada. Logout durante uma renovação aguarda sua resposta para usar o cookie atualizado.

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
| Ctrl+Q / Ctrl+C | Sair do aplicativo |

No login e no cadastro, os botões de ação ficam na mesma linha: **Entrar / Cadastro** ou **Criar conta / Login**. **Selecionar perfil** fica no canto superior direito do formulário. ↓ na senha do servidor foca a ação principal; ←/→ alternam os dois botões, ↓ foca Selecionar perfil e ↑ volta à senha do servidor. ↑ em Selecionar perfil volta à ação principal, e ↓ mantém o foco nesse botão. As setas não dão a volta na tela. Tab segue campos, ação principal, ação secundária e Selecionar perfil; controles indisponíveis são pulados.

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
src/app/         Conexões abertas por instância
src/ui/          Login, perfis, leitura real, demonstração, tema e edição Unicode
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

- [x] Especificação OpenAPI disponível.
- [x] Tela de login, HTTP e aplicativo compilável.
- [x] Testes com backend simulado e pseudoterminal.
- [x] Inventário e fixtures de contrato com validação OpenAPI 3.0.
- [x] Login público, sessão e handshake WS nativo observados na instância local.
- [x] R0: contrato e ambiente de integração validados com a conta fornecida, paginação e cenário privado HTTPS/WSS.
- [x] Transporte WS/WSS em C, com cookies por contexto, filas limitadas e revalidação REST.
- [x] Base de terminal exercitada em Kitty, Konsole e tmux.
- [x] R2: interface navegável com dados simulados em `--demo`, editor multilinha, mouse e ajuda.
- [x] R3: perfis XDG, descoberta por URL, isolamento de contextos e preferências de taxa/burst, com validação automatizada e demonstração no backend real.
- [x] R4: cadastro, login público/privado, renovação serializada, revogação e logout, com identidade REST/WS e cookies somente em memória.
- [x] R5: canais ordenados, tópicos, histórico paginado, autores e metadados em modo de leitura.
- [ ] Envio e atualização automática de conversas pelo backend.

## Ler canais e histórico

Após entrar, a lista de canais abre automaticamente. F6 volta à tela da conta; o botão **Canais / F6** e a tecla F6 permitem reabrir a leitura. Escolha um canal com ↑/↓ e abra com Enter, → ou clique. Categorias são separadores; canais de voz mostram que chamadas ainda não estão disponíveis. A leitura usa apenas o canal aberto, e consultar seu histórico também marca leitura no servidor. `*` indica uma última mensagem ainda não lida nos dados consultados.

A tela segue o layout da demonstração: abas de servidores no topo, com linha separadora e uma linha de margem abaixo, canais à esquerda, histórico e área de edição no painel central. **+ Adicionar**, à direita das abas, abre o formulário de login/cadastro de outro servidor. Depois do login, a nova conexão aparece em uma aba; clique nas abas para trocar de servidor. As sessões ficam separadas e abertas somente nesta execução, até o limite de 64 conexões. Entrar novamente na mesma URL substitui sua conexão anterior. Esc no formulário de adição volta ao servidor anterior; se houver uma conexão em andamento, Esc primeiro a cancela.

No histórico, ↑/↓ e PageUp/PageDown rolam o texto. Home vai ao início da janela carregada; pressione PageUp nesse ponto para buscar a página anterior sem deslocar a mensagem que está lendo. End consulta novamente a página recente. Ctrl+R atualiza o canal aberto ou repete a consulta que falhou; uma falha ao buscar mensagens antigas mantém a janela atual para nova tentativa.

Ao abrir a leitura, o foco começa nas abas. Use ←/→ para escolher o servidor ou destacar **+ Adicionar**, e Enter para abrir o formulário. O botão fica branco sem foco e laranja ao selecionar, quando aparece como **>+ Adicionar<**. ↓ entra na lista de canais.

Em tela ampla, Tab/Shift+Tab alternam entre abas, canais e histórico. ← nos canais, ou ↑ no primeiro canal, volta às abas; ←/→ nas abas trocam de servidor e Enter em **+ Adicionar** abre o formulário. ↓ nas abas foca os canais. Esc alterna canais/histórico. Abaixo de 80 colunas, Tab/Shift+Tab e os controles **Canais / Conversa** alternam o painel visível. F6 volta à conta, Ctrl+L sai da conta e remove sua aba, Ctrl+P abre a seleção de perfil encerrando a sessão local atual, e Ctrl+Q/Ctrl+C fecha o programa. O mínimo dessa tela é 44×14; para abrir o formulário, amplie para 44×22.

Mensagens aparecem em ordem cronológica, com data, autor, indicação de edição e referências de resposta. Anexos e embeds mostram metadados em texto, sem abrir URLs ou baixar mídia. Autores ausentes têm um rótulo de indisponibilidade. A área de edição exibe **Somente leitura**; o envio permanece indisponível. Novas mensagens aparecem quando você atualiza a página recente.

A janela em memória contém até 1.000 mensagens ou 4 MiB de JSON serializado. Ao carregar páginas antigas além desse limite, libera mensagens recentes; End volta a consultá-las. Trocar de canal ou servidor descarta seu histórico local. As abas, sessões e mensagens não são salvas no disco, e abrir a lista de canais não consulta seus históricos em segundo plano.

## Testes

```sh
python -m venv build/test-env
build/test-env/bin/python -m pip install -r requirements-dev.txt
cmake -S . -B build -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
```

Os testes usam credenciais sintéticas e servidores HTTP/WS locais. Cobrem acesso público/privado, cookies, logout, erros, limites de entrada/resposta/fila, cancelamento, URLs com prefixo, senhas ocultas, espaços em senhas, redimensionamento, Backspace repetido, cliques e restauração do terminal. pyte reconstrói a tela para verificar a posição do cursor. A suíte WS inclui fragmentação dentro de UTF-8, chunks, ping/pong intercalado, encerramento, timeouts, sessão revogada com ACK e WSS com CA/hostname verificados. Essas dependências Python são usadas somente nos testes; os mocks não comprovam integração com a instância real.

As suítes `unicode-editor` e `chat-terminal` exercitam a demonstração local: grafemas, largura em células, cursor nas linhas quebradas, colagem multilinha e fragmentada, limites, mouse, atalhos, modais, estados vazio/carregando/erro, isolamento de rascunhos, preservação da leitura, resize e saída. Também conferem os indicadores de foco e erro em terminal sem cores. `history-terminal` verifica a leitura real com backend simulado, abertura automática, paginação, abas, login/cadastro pelo botão de adição, cancelamento, erros e isolamento entre sessões.

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
