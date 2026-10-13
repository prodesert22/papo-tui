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

### Servidores salvos

A abertura normal sempre apresenta a tela de login. Clique em **Selecionar perfil**, ou foque esse botão com Tab e pressione Enter, para abrir o modal de seleção. **Ctrl+P** no login também abre esse modal. ↑/↓ percorrem a lista; Enter ou um clique no perfil selecionam-no e voltam ao login com a URL preenchida. No modal, **Gerenciar / Ctrl+P** abre o gerenciador completo; **Cancelar / Esc** volta ao login e Ctrl+Q/Ctrl+C fecha o aplicativo.

Abra **Gerenciar** no modal ou execute `./build/papo-tui --profiles` para iniciar diretamente no gerenciador. O topo contém duas opções: **Listar** e **+ Adicionar**. Em **Listar**, todos os perfis ficam numa lista vertical; a rolagem acompanha o destaque.

| Área | Controles |
| --- | --- |
| Topo | ← vai para Listar e → para Adicionar, parando nas extremidades; ↓ ou Enter em Listar entram na lista |
| Lista | ↑/↓ mudam o perfil destacado; PageUp/PageDown avançam uma página; Home/End vão ao início/fim |
| Lista → ações | Enter ou → focam Selecionar, o primeiro botão do rodapé |
| Ações | ←/→ movem entre Selecionar, Editar, Remover, Limites e Identificar, parando nas extremidades; Enter executa a ação |
| Ações → lista | ↑ volta à lista com o mesmo perfil destacado |
| Lista → topo | ↑ no primeiro perfil volta ao topo; ← mantém o foco na lista |
| Tab / Shift+Tab | Percorrem topo, lista e botões disponíveis |
| Mouse | Clique num perfil para destacá-lo ou num botão para executar a ação |

Os textos principais, nomes, campos e botões são brancos; o texto dos botões e itens de lista selecionados usa laranja `#ffb347`, e dicas e comandos permanecem cinza. O controle em foco tem um fundo laranja escuro mais evidente, distinto do fundo laranja discreto do perfil quando você está nas ações. As dicas do rodapé mudam conforme a área focada. Setas horizontais nos botões dos modais também param nas extremidades. No modal de seleção aberto pelo login, ↑/↓ percorrem os perfis, → leva aos botões e ↑ nos botões volta à lista.

Sem suporte a cores, o foco usa inversão e o perfil destacado fora do foco usa negrito. Em janelas estreitas, os atalhos do rodapé aparecem abreviados para caber na tela.

**+ Adicionar** aparece somente no topo: clique nela ou destaque-a e pressione Enter para cadastrar um perfil. Enquanto essa opção estiver selecionada, as ações do rodapé ficam indisponíveis e Tab/Shift+Tab e ↓ mantêm o foco no topo. Cancelar o cadastro com Esc mantém esse comportamento; volte a **Listar** para habilitar as ações de um perfil salvo. Salvar o cadastro volta à lista com o novo perfil destacado.

- **+ Adicionar no topo / Ctrl+N:** informar nome e URL.
- **Selecionar:** voltar ao login com a URL do perfil destacado.
- **Editar / Ctrl+E:** renomear ou corrigir a URL, preservando o ID do perfil.
- **Remover / Ctrl+D:** abrir a confirmação; **Cancelar** é a opção inicial.
- **Limites / Ctrl+L:** editar a taxa global de requisições e o burst.
- **Identificar / F5:** repetir a identificação da instância.
- **Ctrl+R:** recarregar o arquivo de configuração.
- **Esc:** cancelar a edição/remoção ou voltar ao login.
- **Ctrl+Q / Ctrl+C:** fechar o aplicativo.

Nos diálogos, Tab percorre campos e botões, ←/→ escolhem o botão, e Enter avança ou confirma. Colagem delimitada nunca salva, seleciona ou remove por conta própria. Ctrl+U limpa o campo; setas, Home/End e Backspace/Delete permitem editar. O gerenciador exige 44 colunas e 14 linhas; o login exige 44×22.

Os perfis aceitam URLs HTTPS, com porta e prefixo opcionais, sem credenciais embutidas, query ou fragmento. Para HTTP de desenvolvimento em `localhost`, `127.0.0.1` ou `[::1]`, inicie com `--allow-local-http`:

```sh
./build/papo-tui --profiles --allow-local-http
```

Perfis HTTP já salvos permanecem visíveis sem essa opção, mas não podem ser selecionados. Ao abrir a seleção de perfil pelo login, a conexão atual é fechada e usuário, senhas e cookies locais são descartados. O perfil selecionado preenche a URL e deixa esse campo bloqueado; para corrigir a URL, volte a **Selecionar perfil → Gerenciar → Editar**. Cada entrada exige novo login.

Os dados públicos são salvos em `$XDG_CONFIG_HOME/papo-tui/config.json`, ou em `~/.config/papo-tui/config.json` quando XDG não estiver definido com caminho absoluto. O diretório é restrito (`700`) e o arquivo usa `600`. Até 64 perfis podem ser salvos; o nome aceita 128 bytes UTF-8, e a URL, 2048. Senhas, cookies, mensagens e rascunhos não entram nesse arquivo.

Um arquivo inválido ou de versão desconhecida fica preservado e bloqueia gravações. Corrija-o e use Ctrl+R para recarregar. Alterações externas também exigem recarregar antes de salvar. Falhas de gravação mantêm a configuração e os perfis anteriores.

Ao destacar um perfil, o aplicativo consulta a disponibilidade e os dados públicos da instância, mostrando seu nome e a condição pública/privada. Essas consultas continuam em segundo plano enquanto você navega ou edita. Se houver falha, use F5 para tentar novamente ou **Editar** para corrigir a URL. A identificação informa disponibilidade; entrar na conta continua exigindo autenticação.

### Taxa e burst

No gerenciador, destaque um perfil salvo e abra **Limites** ou pressione Ctrl+L. Informe a quantidade de requisições por segundo e a capacidade de burst. A taxa deve ser positiva e finita; use ponto decimal, por exemplo `2.5`. O burst aceita inteiros de 1 a 4294967295. Enter avança pelos campos; Tab alterna o foco, e ←/→ escolhem os botões.

**Padrão** preenche taxa 10 e burst 25; confirme com **Salvar** para aplicar. Valores inválidos mantêm o diálogo aberto e a configuração anterior. Esc ou **Cancelar** descarta a edição. As preferências são salvas com os perfis e carregadas ao iniciar, mesmo quando você abre diretamente o login manual. Configurações anteriores continuam compatíveis e usam 10/25 até você salvar outra preferência.

O limite é compartilhado por descoberta, login, cadastro, renovação, logout, revalidação e novas conexões de tempo real. Salvar, recarregar ou trocar de perfil mantém o crédito já disponível e a espera indicada por `Retry-After`; aumentar burst não cria crédito adicional naquele instante. Diminuir burst restringe o crédito restante à nova capacidade. A interface continua aceitando teclas enquanto as requisições aguardam capacidade.

### Login

A tela apresenta um cabeçalho, o formulário e uma área de estado e controles. Preencha os quatro campos:

| Campo | O que informar |
| --- | --- |
| URL do servidor | URL completa, como `https://chat.exemplo.com` |
| Usuário | Seu username nessa instância |
| Senha | A senha da sua conta |
| Senha do servidor | Obrigatória em instâncias privadas; deixe vazia em instâncias públicas |

Para um servidor local, inicie com `./build/papo-tui --allow-local-http` e use `http://127.0.0.1:8080`. HTTP também é aceito em `localhost` e `[::1]` com essa opção; servidores remotos exigem HTTPS. Prefixos de caminho são preservados, como em `https://exemplo.com/papo`.

Selecione **Entrar** ou pressione Enter. O aplicativo consulta o servidor, solicita acesso à instância privada quando necessário e autentica sua conta. As senhas são ocultas; credenciais e cookies não são salvos entre execuções.

Quando a instância é privada e a senha do servidor foi esquecida, o campo **Senha do servidor** ganha uma borda vermelha, um marcador de obrigatório e recebe o foco automaticamente. Digite a senha e pressione Enter para repetir a tentativa. Se essa senha for rejeitada, o campo é limpo e focado para correção. A borda desaparece ao começar a editar; usuário e senha da conta permanecem no formulário durante essa correção.

### Criar uma conta

Clique em **Cadastro**, foque o botão com Tab e Enter, ou pressione **Ctrl+N**. O formulário acrescenta **Confirmar senha** logo após a senha; URL e usuário são preservados ao alternar entre login e cadastro, e as senhas são limpas.

Informe um usuário com pelo menos 3 caracteres e uma senha com pelo menos 8 caracteres, incluindo uma letra maiúscula e um caractere especial. A instância pode impor outros limites. As senhas permanecem mascaradas. Instâncias privadas também exigem a senha do servidor antes de criar a conta.

**Confirmar senha** é mascarado e serve somente à validação local. A senha precisa coincidir exatamente com a confirmação. Campo vazio ou senhas diferentes bloqueiam o envio e focam a confirmação; senha curta, sem maiúscula ou sem caractere especial mostra uma mensagem específica e foca a senha. O erro aparece em vermelho, em até duas linhas para permanecer legível em janelas estreitas.

Confirme **Criar conta** ou pressione Enter em um campo. Erros locais focam o campo para correção; usuário já existente também recebe foco. Rejeições adicionais mostram uma mensagem da aplicação. Nenhum detalhe remoto contendo credenciais aparece na tela.

Ao receber a confirmação, a tela volta ao login, mantém URL e usuário e foca a senha vazia. Digite a senha novamente e confirme **Entrar**. Criar uma conta exige esse login explícito. **Login / Ctrl+N** volta ao formulário de entrada sem enviar cadastro.

Se a autorização privada expirar, o campo **Senha do servidor** é limpo, recebe borda vermelha e foco. Informe a senha e confirme novamente. A aplicação não repete o cadastro automaticamente. Se um envio for interrompido sem confirmação, tente entrar antes de reenviar: a conta pode ter sido criada.

### Sessão, renovação e saída

Depois de autenticar, a tela mostra a conta, o servidor e o estado da conexão de tempo real. A tela de conversas usa dados locais em **--demo**; canais e mensagens reais serão integrados no próximo marco.

A sessão é revalidada por REST a cada 30 segundos. A primeira renovação ocorre até 30 segundos após o login; as seguintes são agendadas antes da expiração informada pelo servidor, com margem de 60 segundos para sessões longas. Validades curtas usam um intervalo menor. **Renovar / Ctrl+R** permite renovar imediatamente. Os campos ficam bloqueados enquanto uma operação aguarda resposta; Esc cancela a operação e encerra a sessão local.

A aplicação realiza uma renovação por vez e confirma a identidade depois de receber o novo cookie. Consultas que receberem 401 podem tentar uma única renovação. Se ela falhar, ou se a sessão for revogada, a conta for bloqueada ou a identidade não puder ser confirmada, os cookies e eventos locais são descartados e a senha recebe foco para novo login. Cadastro, login e outras mutações não são repetidos automaticamente.

Heartbeat/ACK de tempo real não comprova autenticação. Quando a conexão de tempo real é interrompida, **Reconectar / Ctrl+R** aparece para refazê-la enquanto a sessão REST continua válida. Falhas de rede e de autenticação aparecem na área de estado.

**Sair da conta** fecha a conexão de tempo real, solicita a revogação e limpa a sessão local. Durante uma renovação já enviada, aguarda sua resposta para revogar com o cookie atualizado. Se não conseguir confirmar a revogação remota, informa isso e conclui a limpeza local. Reabrir o programa sempre exige login; senhas e cookies não são persistidos.

O aplicativo compartilha um limite configurável entre todas as tentativas de conexão, com padrões de 10 requisições por segundo e burst 25. Ajuste esses valores em **Selecionar perfil → Gerenciar → Limites**. Ao atingir o limite, os envios aguardam e a interface continua respondendo.

## Teclado e mouse

O tema usa fundo quase preto, painéis em cinza escuro e laranja `#ffb347` em títulos, seleções e foco, tanto no login quanto na demonstração. Terminais com paleta fixa usam uma aproximação do tom; marcadores de foco e seleção também permanecem visíveis sem cores.

| Controle | Ação |
| --- | --- |
| Tab / Shift+Tab | Percorrer campos e botões na ordem visual |
| ↑ / ↓ | Percorrer campos e linhas de botões, parando nas extremidades |
| Enter | Confirmar login/cadastro ou executar o botão focado |
| Ctrl+N | Alternar login/cadastro antes de autenticar |
| Ctrl+R | Renovar a sessão ou reconectar o tempo real interrompido |
| ← / → | Mover o cursor no campo; alternar entre os botões da mesma linha |
| Home / End | Mover o cursor para o início/fim do campo |
| Backspace / Delete | Apagar texto |
| Ctrl+U | Limpar o campo atual |
| Ctrl+P | Abrir a seleção de perfil, encerrando a conexão e limpando credenciais locais |
| Clique esquerdo no campo | Focar e posicionar o cursor |
| Clique esquerdo no botão | Executar login, cadastro, renovação, reconexão, logout ou seleção de perfil |
| Esc | Cancelar uma conexão em andamento |
| Ctrl+Q / Ctrl+C | Fechar o aplicativo |

No login e no cadastro, os botões de ação ficam na mesma linha: **Entrar / Cadastro** ou **Criar conta / Login**. **Selecionar perfil** fica no canto superior direito do formulário. ↓ na senha do servidor foca a ação principal; ←/→ alternam os dois botões, ↓ foca Selecionar perfil e ↑ volta à senha do servidor. ↑ em Selecionar perfil volta à ação principal, e ↓ mantém o foco nesse botão. As setas não dão a volta na tela. Tab segue campos, ação principal, ação secundária e Selecionar perfil; controles indisponíveis são pulados.

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

Ctrl+Q abre uma confirmação quando existe qualquer rascunho. **Voltar** é a opção inicial; Tab ou ←/→ escolhem outra opção, e Enter confirma. Os botões também aceitam clique. Pressionar Ctrl+Q novamente confirma a saída. Ctrl+C e sinais de encerramento fecham diretamente. Todos os rascunhos e mensagens locais são descartados ao sair. **+ Adicionar** na demonstração apenas abre um aviso; para cadastrar perfis reais, use **Selecionar perfil → Gerenciar → Adicionar** no login.

## Problemas ao conectar

Confira a URL, a disponibilidade do servidor e as credenciais da conta nessa instância. Em servidores privados, confira também a senha do servidor. A tela informa erros de autenticação e rede.

Se o servidor redireciona a URL, informe o endereço final. Para HTTPS, o certificado precisa ser válido e confiável no sistema. Você pode cancelar uma tentativa com Esc e tentar novamente.

Se sua instância usa uma autoridade certificadora própria, indique seu certificado CA:

```sh
CURL_CA_BUNDLE=/caminho/ca.pem ./build/papo-tui
```

A validação do certificado e do nome do servidor permanece ativa.

Se a tela informar que o limite de requisições foi atingido, aguarde e tente novamente. O aplicativo respeita a espera indicada pelo servidor em `Retry-After` (até 24 horas); quando esse valor está ausente ou inválido, aguarda 3 segundos. A tentativa anterior não é repetida automaticamente.
