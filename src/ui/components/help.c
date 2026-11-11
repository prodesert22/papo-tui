#include "ui/components/help.h"
#include "model/text/translation.h"
#include "net/voice/client.h"
#include "ui/components/button.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FOCUS_SWITCH, FOCUS_CLOSE, FOCUS_SEARCH, FOCUS_TABLE };

typedef struct {
    help_context contexts;
    const char *keys, *function;
} help_row;
static const help_row rows[] = {
    {HELP_COMMON, N_("Ctrl+G / F1"), N_("Abrir ou fechar a ajuda do contexto atual")},
    {HELP_COMMON, N_("Ctrl+/"), N_("Abrir ou fechar a ajuda global")},
    {HELP_COMMON, N_("Ctrl+F na ajuda"), N_("Focar a busca por teclas, função ou área")},
    {HELP_COMMON, N_("Tab / Shift+Tab na ajuda"), N_("Percorrer busca, tabela e botões")},
    {HELP_COMMON, N_("Ctrl+Q"), N_("Sair do aplicativo")},
    {HELP_CONVERSATION | HELP_LOGIN | HELP_ACCOUNT | HELP_PROFILES, N_("Tab / Shift+Tab"),
     N_("Percorrer as áreas e os botões")},
    {HELP_CONVERSATION, N_("Alt+← / → / ↑ / ↓"), N_("Ir à área ou ao botão naquela direção")},
    {HELP_CONVERSATION, N_("Ctrl+S"), N_("Abrir configurações do aplicativo")},
    {HELP_CONVERSATION, N_("Ctrl+F"), N_("Buscar mensagens no servidor")},
    {HELP_CONVERSATION, N_("Ctrl+K"),
     N_("Ir para um canal ou DM de qualquer servidor pelo nome; mostra primeiro menções e não "
        "lidas")},
    {HELP_CONVERSATION, N_("Ctrl+B"), N_("Abrir mensagens fixadas")},
    {HELP_CONVERSATION, N_("Ctrl+U"), N_("Abrir ou fechar a lista de membros")},
    {HELP_CONVERSATION, N_("Ctrl+D"), N_("Abrir a central de notificações")},
    {HELP_CONVERSATION | HELP_ACCOUNT, N_("Ctrl+T"),
     N_("Abrir a conta; na conta, voltar às conversas")},
    {HELP_CONVERSATION | HELP_LOGIN | HELP_ACCOUNT, N_("Ctrl+P"),
     N_("Selecionar ou gerenciar perfis de servidores")},
    {HELP_CONVERSATION, N_("Ctrl+X"),
     N_("Voltar à mensagem do áudio em segundo plano e abrir o player")},
    {HELP_CONVERSATION, N_("Ctrl+L"), N_("Sair da conta atual")},
    {HELP_CONVERSATION, N_("Ctrl+R"), N_("Atualizar a conversa e permissões ou reconectar")},
    {HELP_CONVERSATION, N_("F5"), N_("Reconectar agora quando a conexão cair")},
    {HELP_SERVER_TABS, N_("← / →"), N_("Selecionar um servidor ou Adicionar")},
    {HELP_SERVER_TABS, N_("Enter"), N_("Ativar o servidor ou abrir a conexão de outro servidor")},
    {HELP_SERVER_TABS, N_("↓"), N_("Entrar nos controles Canais/DMs")},
    {HELP_SERVER_TABS, N_("[+ Adicionar]"), N_("Conectar outro servidor")},
    {HELP_SERVER_TABS, N_("Ctrl+Espaço / m / clique direito"),
     N_("Abrir o menu do servidor, com Configurações do servidor e Remover servidor")},
    {HELP_SERVER_TABS, N_("Delete"),
     N_("Remover o servidor da lista e apagar a sessão salva neste computador")},
    {HELP_HEADER | HELP_CHANNELS, N_("[Canais] / [DMs]"),
     N_("Alternar a lista de canais e mensagens diretas")},
    {HELP_CHANNELS, N_("[⚙] / [*]"), N_("Abrir as configurações do aplicativo")},
    {HELP_HEADER, N_("← / →"), N_("Percorrer Canais/DMs e os botões do histórico")},
    {HELP_HEADER, N_("Enter"), N_("Abrir o botão selecionado")},
    {HELP_HEADER, N_("↑ / ↓ / Esc"), N_("Voltar aos servidores, à lista ou ao histórico")},
    {HELP_HEADER | HELP_HISTORY, N_("🔍 / 🖈 / 👤"),
     N_("Clicar para abrir busca, mensagens fixadas ou membros")},
    {HELP_CHANNELS, N_("↑↓ / Home / End"), N_("Escolher um canal ou DM; categorias são ignoradas")},
    {HELP_CHANNELS, N_("PgUp / PgDn"), N_("Percorrer páginas da lista")},
    {HELP_CHANNELS, N_("Enter / →"), N_("Abrir um canal de texto ou DM")},
    {HELP_CHANNELS | HELP_VOICE, N_("Enter em voz"), N_("Conectar à chamada do canal de voz")},
    {HELP_CHANNELS | HELP_SETTINGS_BUTTON | HELP_VOICE, N_("🔊 / 🎤 / ♪ / ●"),
     N_("Alternar áudio e microfone nos botões ao lado de Configurações")},
    {HELP_CHANNELS | HELP_SETTINGS_BUTTON | HELP_VOICE, N_("← / →"),
     N_("Alternar o foco entre áudio, microfone e Configurações")},
    {HELP_CHANNELS | HELP_VOICE, N_("[Sair da chamada]"),
     N_("Encerrar a chamada; navegar preserva a conexão")},
    {HELP_CONVERSATION | HELP_VOICE, N_("Tecla configurada / segurar botão"),
     N_("Transmitir com pressionar para falar; a combinação ou o botão 4/5 do mouse é "
        "escolhido em Configurações > Áudio")},
    {HELP_CHANNELS, N_("Ctrl+O / Shift+Enter"),
     N_("Abrir o histórico do canal selecionado, inclusive voz")},
    {HELP_CHANNELS | HELP_MEMBERS, N_("Ctrl+Espaço / m"),
     N_("Abrir o menu de contexto da seleção")},
    {HELP_CHANNELS | HELP_MEMBERS | HELP_HISTORY | HELP_SELECTION, N_("Clique direito"),
     N_("Abrir o menu do canal, DM, membro ou mensagem")},
    {HELP_DM_LIST, N_("Ctrl+W"), N_("Fechar a DM selecionada")},
    {HELP_CHANNELS, N_("↑ no início / Esc"), N_("Voltar aos controles da lista ou ao histórico")},
    {HELP_HISTORY, N_("↑↓ / roda"), N_("Rolar o histórico sem mudar de conversa")},
    {HELP_HISTORY, N_("Arrastar o mouse"),
     N_("Selecionar texto do histórico e copiá-lo ao soltar")},
    {HELP_HISTORY, N_("PgUp / PgDn"),
     N_("Rolar uma página e carregar mensagens ao chegar às bordas")},
    {HELP_HISTORY, N_("Home / End"), N_("Ir ao início ou ao fim do histórico carregado")},
    {HELP_HISTORY | HELP_EDITOR | HELP_SELECTION, N_("Ctrl+J"),
     N_("Entrar ou sair da seleção de mensagens")},
    {HELP_HISTORY, N_("Ctrl+Espaço"), N_("Selecionar uma mensagem e abrir suas ações")},
    {HELP_HISTORY, N_("Digite texto"), N_("Focar o editor e continuar o rascunho")},
    {HELP_HISTORY, N_("← / Esc"), N_("Voltar à lista de canais")},
    {HELP_HISTORY, N_("↑ no início"), N_("Focar os botões Busca, Fixadas e Membros")},
    {HELP_EDITOR, N_("Enter"), N_("Enviar ou confirmar a edição; sem texto, perguntar se exclui")},
    {HELP_EDITOR, N_("Shift+Enter / Ctrl+N"), N_("Inserir uma quebra de linha sem enviar")},
    {HELP_EDITOR, N_("Ctrl+A"), N_("Selecionar todo o texto da caixa")},
    {HELP_EDITOR | HELP_HISTORY, N_("Ctrl+E"), N_("Abrir ou fechar o seletor de emojis")},
    {HELP_EDITOR | HELP_HISTORY, N_("Ctrl+O / 📎"), N_("Escolher arquivos para anexar")},
    {HELP_EDITOR | HELP_HISTORY, N_("Ctrl+V"),
     N_("Colar arquivos ou imagens do clipboard, quando disponível")},
    {HELP_EDITOR, N_("Ctrl+Y"),
     N_("Tentar novamente o último envio com falha; envios incertos pedem confirmação")},
    {HELP_EDITOR, N_("Ctrl+C"), N_("Limpar o campo de edição")},
    {HELP_EDITOR | HELP_FIELDS, N_("Ctrl+W / Ctrl+Backspace"), N_("Apagar a palavra anterior")},
    {HELP_EDITOR | HELP_FIELDS, N_("Ctrl+← / Ctrl+→"), N_("Mover o cursor por palavras")},
    {HELP_EDITOR | HELP_FIELDS, N_("Shift+setas"), N_("Selecionar texto por caracteres ou linhas")},
    {HELP_EDITOR | HELP_FIELDS, N_("Ctrl+Shift+setas"), N_("Selecionar texto por palavras")},
    {HELP_EDITOR | HELP_FIELDS, N_("Ctrl+Shift+C"),
     N_("Copiar a seleção; sem seleção, copiar o campo")},
    {HELP_EDITOR | HELP_FIELDS, N_("Home / End"), N_("Mover ao início ou ao fim da linha")},
    {HELP_EDITOR, N_("PgUp / PgDn"), N_("Rolar o histórico uma linha mantendo o editor")},
    {HELP_EDITOR, N_("Shift+PgUp/PgDn"),
     N_("Rolar uma página quando o terminal encaminha a combinação")},
    {HELP_EDITOR, N_("↑ com editor vazio"), N_("Editar a última mensagem própria carregada")},
    {HELP_EDITOR, N_("@ / :nome"), N_("Abrir sugestões de menções ou emojis")},
    {HELP_EDITOR, N_("/ no início"), N_("Listar os comandos; ↑↓ escolhe, Tab completa, Esc fecha")},
    {HELP_EDITOR, N_("/nick nome"), N_("Mudar seu apelido neste servidor; /nick sozinho o remove")},
    {HELP_EDITOR, N_("/status online|ausente|ocupado"), N_("Mudar seu status neste servidor")},
    {HELP_EDITOR, N_("/bio texto"), N_("Mudar a descrição do seu perfil; /bio sozinho a apaga")},
    {HELP_EDITOR, N_("/avatar / /banner"), N_("Escolher a imagem do avatar ou do banner")},
    {HELP_EDITOR, N_("/banner remover"), N_("Remover o banner do seu perfil")},
    {HELP_EDITOR, N_("//texto"), N_("Enviar uma mensagem que começa com /")},
    {HELP_EDITOR, N_("↑↓ / Tab / Enter"),
     N_("Escolher e inserir a sugestão quando a lista está aberta")},
    {HELP_EDITOR, N_("Esc"),
     N_("Fechar sugestões, cancelar resposta/edição ou voltar ao histórico")},
    {HELP_EDITOR, N_("←→ nos anexos"), N_("Escolher um anexo; Enter abre a remoção")},
    {HELP_SELECTION, N_("↑↓"), N_("Escolher a mensagem ou carregar a próxima página")},
    {HELP_SELECTION, N_("Ctrl+Espaço / m"), N_("Abrir as ações da mensagem selecionada")},
    {HELP_SELECTION, N_("r / e / E"),
     N_("Responder, adicionar reação ou ver quem reagiu, se permitido")},
    {HELP_SELECTION, N_("←→ / Enter"), N_("Escolher e abrir a mídia da mensagem")},
    {HELP_SELECTION, N_("Esc / Tab"),
     N_("Sair da seleção e voltar ao foco anterior ou à próxima área")},
    {HELP_SETTINGS_BUTTON, N_("Enter / Espaço"), N_("Abrir as configurações do aplicativo")},
    {HELP_SETTINGS_BUTTON, N_("↑ / ↓"), N_("Focar a lista de canais ou o histórico")},
    {HELP_SETTINGS_BUTTON | HELP_EDITOR_BUTTONS, N_("Tab / Shift+Tab"),
     N_("Focar a próxima área ou voltar")},
    {HELP_EDITOR_BUTTONS, N_("← / →"), N_("Escolher um anexo ou um dos botões Anexar e Emojis")},
    {HELP_EDITOR_BUTTONS, N_("Enter"),
     N_("Abrir o botão selecionado ou a confirmação de remover anexo")},
    {HELP_EDITOR_BUTTONS, N_("Ctrl+O / Ctrl+E"),
     N_("Anexar arquivos ou abrir o seletor de emojis")},
    {HELP_EDITOR_BUTTONS | HELP_SETTINGS_BUTTON, N_("Esc"), N_("Voltar ao editor ou ao histórico")},
    {HELP_MENU, N_("↑↓ / Enter / clique"), N_("Escolher e executar uma opção disponível")},
    {HELP_MENU, N_("← / Esc"), N_("Voltar do submenu ou fechar o menu")},
    {HELP_MENU, N_("Ctrl+Espaço / m"), N_("Fechar o menu")},
    {HELP_MENU, N_("Ctrl+R"), N_("Atualizar os dados do menu")},
    {HELP_MEMBERS, N_("↑↓ / Home / End"), N_("Escolher um membro")},
    {HELP_MEMBERS, N_("PgUp / PgDn"), N_("Percorrer páginas de membros")},
    {HELP_MEMBERS, N_("/ / Buscar"), N_("Focar a busca por nome ou @username")},
    {HELP_MEMBERS, N_("Tab / Shift+Tab"), N_("Percorrer busca, agrupamento e membros")},
    {HELP_MEMBERS, N_("Enter / clique"), N_("Abrir o perfil ou alterar o agrupamento")},
    {HELP_MEMBERS, N_("Ctrl+R"), N_("Atualizar a lista de membros")},
    {HELP_MEMBERS, N_("Esc / Ctrl+U"), N_("Voltar do painel ou fechar a lista")},
    {HELP_MEMBER_SEARCH, N_("Enter / Tab"), N_("Voltar à lista de membros ou ao agrupamento")},
    {HELP_MEMBER_SEARCH, N_("Esc / Ctrl+U"), N_("Fechar a lista de membros")},
    {HELP_MEMBER_SEARCH, N_("Ctrl+R"), N_("Atualizar os membros")},
    {HELP_MEMBER_PROFILE, N_("↑↓ / roda"),
     N_("Rolar o perfil; botões iniciam DM ou ações permitidas")},
    {HELP_MEMBER_PROFILE | HELP_CONFIRM, N_("Tab / ←→ / Enter"),
     N_("Escolher um botão e confirmar; ações destrutivas não são automáticas")},
    {HELP_MEMBER_PROFILE | HELP_CONFIRM, N_("Esc"), N_("Cancelar e voltar sem executar a ação")},
    {HELP_EMOJIS, N_("Tab / Shift+Tab"), N_("Percorrer busca, fontes e emojis")},
    {HELP_EMOJIS, N_("↑↓←→ / Enter"), N_("Escolher e inserir um emoji ou reação")},
    {HELP_EMOJIS, N_("Ctrl+R"), N_("Atualizar os emojis do servidor")},
    {HELP_EMOJIS, N_("Esc / Ctrl+E"), N_("Fechar o seletor sem alterar o rascunho")},
    {HELP_NOTIFICATIONS, N_("↑↓ / roda"), N_("Escolher uma notificação")},
    {HELP_NOTIFICATIONS, N_("Digite / Tab"), N_("Buscar no conteúdo ou percorrer filtros e ações")},
    {HELP_NOTIFICATIONS, N_("←→ / Enter"),
     N_("Mudar o filtro ou abrir os detalhes da notificação")},
    {HELP_NOTIFICATIONS, N_("[Abrir conversa]"),
     N_("Ler a conversa e localizar a mensagem selecionada")},
    {HELP_NOTIFICATIONS, N_("[Preferências]"), N_("Configurar avisos no desktop e a prévia")},
    {HELP_NOTIFICATIONS, N_("[Mais antigas]"), N_("Carregar mais notificações")},
    {HELP_NOTIFICATIONS, N_("[Nome] / filtros"), N_("Selecionar uma conversa ou ordenar por data")},
    {HELP_NOTIFICATIONS | HELP_SEARCH, N_("Ctrl+R / Esc"),
     N_("Atualizar/tentar novamente ou voltar ao painel anterior")},
    {HELP_SEARCH, N_("Tab / Shift+Tab"), N_("Percorrer consulta, filtros, busca e resultados")},
    {HELP_SEARCH, N_("↑↓ / PgUp / PgDn"), N_("Percorrer resultados ou rolar os detalhes")},
    {HELP_SEARCH, N_("Enter / clique"),
     N_("Buscar, abrir detalhes, localizar mensagem ou executar a ação selecionada")},
    {HELP_REACTIONS, N_("↑↓ / roda"), N_("Rolar as reações ou as pessoas")},
    {HELP_REACTIONS, N_("←→ / Tab"), N_("Alternar as colunas de reações e pessoas")},
    {HELP_REACTIONS, N_("Ctrl+R / Esc"), N_("Atualizar a lista ou fechar o painel")},
    {HELP_FILES, N_("Tab / Shift+Tab"), N_("Percorrer caminho, locais, arquivos e botões")},
    {HELP_FILES, N_("↑↓ / Enter"), N_("Escolher um arquivo, abrir uma pasta ou confirmar")},
    {HELP_FILES, N_("Espaço"), N_("Marcar ou desmarcar um arquivo")},
    {HELP_FILES, N_("Ctrl+V"), N_("Colar arquivos ou imagens quando o clipboard está disponível")},
    {HELP_FILES, N_("Esc"), N_("Cancelar a seleção de arquivos")},
    {HELP_SETTINGS, N_("Tab / ↑↓"),
     N_("Percorrer as configurações e os modos Normal, Light e Ultralight")},
    {HELP_SETTINGS, N_("←→ / clique nas abas"),
     N_("Trocar entre Perfil, Aparência, Áudio, Acessibilidade, Atalhos e Conta")},
    {HELP_SETTINGS, N_("Enter / clique"), N_("Aplicar ou cancelar as configurações")},
    {HELP_SETTINGS, N_("Áudio: ↑↓ / ←→ / Enter"),
     N_("Escolher dispositivos, ajustar volumes, testar o microfone e configurar pressionar para "
        "falar")},
    {HELP_SETTINGS, N_("Tecla para falar: Enter"),
     N_("Capturar qualquer combinação ou os botões 4/5 do mouse; Esc cancela a captura")},
    {HELP_SETTINGS, N_("Perfil: ↑↓ / Enter"),
     N_("Editar apelido, frases, descrição, status, avatar e banner deste servidor; Enter salva")},
    {HELP_SETTINGS, N_("Perfil: Shift+Enter"), N_("Quebrar linha na descrição")},
    {HELP_SETTINGS, N_("Conta: Alterar senha"),
     N_("Trocar a senha deste servidor após confirmar; encerra todas as sessões da conta, "
        "inclusive "
        "esta")},
    {HELP_SETTINGS, N_("Esc / Ctrl+S"), N_("Fechar as configurações")},
    {HELP_SERVER_SETTINGS, N_("←→ nas abas / clique"),
     N_("Trocar entre Visão geral, Cargos, Canais, Emojis, Membros e Auditoria; só aparecem as "
        "abas que suas permissões permitem")},
    {HELP_SERVER_SETTINGS, N_("Tab / Shift+Tab"), N_("Percorrer abas, lista, formulário e Fechar")},
    {HELP_SERVER_SETTINGS, N_("↑↓ / PgUp / PgDn na lista"),
     N_("Escolher um item; Enter abre o formulário, Carregar mais busca a próxima página")},
    {HELP_SERVER_SETTINGS, N_("Membros: digitar na lista"), N_("Filtrar membros pelo nome")},
    {HELP_SERVER_SETTINGS, N_("Auditoria: ▾ Filtros"),
     N_("Filtrar por entidade, ação, quem fez e datas (DD/MM/AAAA); Aplicar filtros recarrega")},
    {HELP_SERVER_SETTINGS, N_("↑↓ / Enter / Espaço no formulário"),
     N_("Editar campos, marcar caixas e acionar botões; Enter num campo salva")},
    {HELP_SERVER_SETTINGS, N_("← / → numa opção"), N_("Trocar o valor da opção")},
    {HELP_SERVER_SETTINGS, N_("Botões de exclusão"), N_("Pressione duas vezes para confirmar")},
    {HELP_SERVER_SETTINGS, N_("Esc / [Fechar]"), N_("Fechar as configurações do servidor")},
    {HELP_IMAGE, N_("← / →"), N_("Trocar a imagem na galeria")},
    {HELP_MEDIA, N_("Ctrl+B / i / [Info]"), N_("Mostrar ou ocultar as propriedades da mídia")},
    {HELP_MEDIA, N_("PgUp / PgDn / roda"), N_("Rolar as propriedades quando abertas")},
    {HELP_MEDIA, N_("Esc"), N_("Fechar a mídia e voltar ao histórico")},
    {HELP_MEDIA_LIST, N_("↑↓ / Enter / Esc"),
     N_("Escolher e abrir uma mídia, ou voltar ao histórico")},
    {HELP_MEDIA_ERROR, N_("Enter"), N_("Tentar carregar a mídia novamente")},
    {HELP_PLAYER, N_("Espaço"), N_("Pausar ou continuar a reprodução")},
    {HELP_PLAYER, N_("← / →"), N_("Retroceder ou avançar 5 segundos")},
    {HELP_PLAYER, N_(", / . ou < / >"), N_("Retroceder ou avançar 1 segundo")},
    {HELP_PLAYER, N_("↑ / ↓"), N_("Aumentar ou diminuir o volume")},
    {HELP_PLAYER, N_("m"), N_("Silenciar ou reativar o som")},
    {HELP_PLAYER, N_("Clique / arraste"), N_("Mover a posição ou o volume nas barras do player")},
    {HELP_AUDIO, N_("[ / ] ou [−] / [+]"), N_("Diminuir ou aumentar a velocidade (0,5× a 2×)")},
    {HELP_AUDIO, N_("l / [Repetir]"), N_("Repetir o áudio ao terminar")},
    {HELP_AUDIO, N_("Espaço no fim"), N_("Recomeçar o áudio do início")},
    {HELP_AUDIO, N_("x / [Parar]"), N_("Parar o áudio e fechar o player")},
    {HELP_AUDIO, N_("Esc / clique fora"),
     N_("Fechar o player e continuar ouvindo em segundo plano")},
    {HELP_LOGIN, N_("Tab / ↑↓ / Enter"),
     N_("Percorrer os campos e botões; Entrar autentica, Cadastro cria uma conta")},
    {HELP_LOGIN, N_("Ctrl+N"), N_("Alternar Login/Cadastro quando desconectado")},
    {HELP_LOGIN, N_("[Redefinir senha]"),
     N_("Criar uma nova senha com o link de redefinição recebido da administração")},
    {HELP_LOGIN | HELP_ACCOUNT, N_("Abas no topo / ← → / Enter"),
     N_("Abrir outro servidor conectado; ↑ ou Shift+Tab a partir do primeiro campo chega às abas")},
    {HELP_LOGIN | HELP_ACCOUNT, N_("Clique direito / m nas abas"),
     N_("Abrir o menu do servidor, com Remover servidor")},
    {HELP_LOGIN | HELP_ACCOUNT, N_("Delete nas abas"),
     N_("Remover o servidor da lista e apagar a sessão salva; um servidor offline tenta "
        "a sessão salva de novo ao ser aberto")},
    {HELP_ACCOUNT, N_("Ctrl+R"), N_("Renovar ou reconectar uma sessão existente")},
    {HELP_ACCOUNT, N_("[Canais]"), N_("Voltar às conversas da sessão aberta")},
    {HELP_ACCOUNT, N_("[Sair da conta]"), N_("Encerrar a sessão e desativar sua recuperação")},
    {HELP_LOGIN | HELP_ACCOUNT, N_("Esc"), N_("Cancelar uma conexão ou voltar à sessão anterior")},
    {HELP_PROFILES, N_("↑↓ / ←→ / Tab"), N_("Percorrer perfis, abas e botões")},
    {HELP_PROFILES, N_("Enter / [Selecionar]"), N_("Usar o perfil e voltar ao login")},
    {HELP_PROFILES, N_("Ctrl+P / [Gerenciar]"), N_("Abrir o gerenciador de perfis")},
    {HELP_PROFILES, N_("Ctrl+N / Ctrl+E"),
     N_("Adicionar ou editar um perfil fora dos campos de edição")},
    {HELP_PROFILES, N_("Ctrl+D"), N_("Abrir a confirmação de remoção do perfil")},
    {HELP_PROFILES, N_("Ctrl+L"), N_("Configurar o orçamento de requisições")},
    {HELP_PROFILES, N_("Ctrl+Y / [Verificar]"), N_("Identificar novamente o servidor selecionado")},
    {HELP_PROFILES, N_("Ctrl+R"), N_("Recarregar os perfis salvos")},
    {HELP_PROFILES, N_("Esc / [Cancelar]"), N_("Fechar um formulário ou voltar sem selecionar")},
    {HELP_FIELDS, N_("Ctrl+C"), N_("Limpar o campo de texto")},
    {HELP_FIELD_CLEAR, N_("Ctrl+U"), N_("Limpar o campo de login ou perfil")},
    {HELP_FIELDS, N_("Colar texto"), N_("Inserir texto sem enviar o formulário")},
};

static const char *row_scope(help_context context)
{
    static const struct {
        help_context context;
        const char *name;
    } names[] = {{HELP_COMMON, N_("Aplicativo")},
                 {HELP_CONVERSATION, N_("Conversas")},
                 {HELP_LOGIN, N_("Conta/login")},
                 {HELP_PROFILES, N_("Perfis")},
                 {HELP_EDITOR, N_("Editor")},
                 {HELP_FIELDS, N_("Formulários")},
                 {HELP_HISTORY, N_("Histórico")},
                 {HELP_CHANNELS, N_("Canais/DMs")},
                 {HELP_MEMBERS, N_("Membros")},
                 {HELP_EMOJIS, N_("Emojis")},
                 {HELP_NOTIFICATIONS, N_("Notificações")},
                 {HELP_SEARCH, N_("Busca/Fixadas")},
                 {HELP_REACTIONS, N_("Reações")},
                 {HELP_FILES, N_("Arquivos")},
                 {HELP_SETTINGS, N_("Configurações")},
                 {HELP_SERVER_SETTINGS, N_("Configurações do servidor")},
                 {HELP_MENU, N_("Menu")},
                 {HELP_MEDIA, N_("Mídia")},
                 {HELP_AUDIO, N_("Áudio")},
                 {HELP_PLAYER, N_("Player")},
                 {HELP_CONFIRM, N_("Confirmação")},
                 {HELP_MEMBER_PROFILE, N_("Perfil de membro")},
                 {HELP_HEADER, N_("Botões")},
                 {HELP_SERVER_TABS, N_("Servidores")},
                 {HELP_SELECTION, N_("Seleção de mensagens")},
                 {HELP_EDITOR_BUTTONS, N_("Botões do editor")},
                 {HELP_SETTINGS_BUTTON, N_("Botão Configurações")},
                 {HELP_IMAGE, N_("Galeria")},
                 {HELP_MEDIA_LIST, N_("Lista de mídias")},
                 {HELP_MEDIA_ERROR, N_("Erro de mídia")},
                 {HELP_ACCOUNT, N_("Conta")},
                 {HELP_MEMBER_SEARCH, N_("Busca de membros")},
                 {HELP_FIELD_CLEAR, N_("Formulários")},
                 {HELP_DM_LIST, N_("Lista de DMs")}};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (context & names[i].context)
            return _(names[i].name);
    return _("Navegação");
}
static bool row_text(const help_modal *h, size_t i, text_buffer *text)
{
    char description[512];
    snprintf(description, sizeof(description), "%s%s%s",
             h->global ? row_scope(rows[i].contexts) : "", h->global ? ": " : "",
             _(rows[i].function));
    return text_decode(text, description);
}

static void filter_rows(help_modal *h)
{
    char query[TEXT_UTF8_BYTES];
    if (!text_encode(&h->search, query))
        query[0] = 0;
    char *needle = g_utf8_casefold(query, -1);
    for (size_t i = 0; i < G_N_ELEMENTS(rows); ++i) {
        bool visible = !(rows[i].contexts & HELP_VOICE) || voice_available();
        if (!needle || !*needle) {
            h->matches[i] = visible;
            continue;
        }
        char entry[768];
        snprintf(entry, sizeof(entry), "%s %s %s", _(rows[i].keys), row_scope(rows[i].contexts),
                 _(rows[i].function));
        char *fold = g_utf8_casefold(entry, -1);
        h->matches[i] = visible && fold && strstr(fold, needle);
        g_free(fold);
    }
    g_free(needle);
    h->scroll[0] = h->scroll[1] = 0;
}
/* Contextual help keeps its area name on row 2, so search and the table
 * start one row lower there. */
static int search_row(const help_modal *h)
{
    return h->global ? 2 : 3;
}

static void switch_help(help_modal *h, bool global)
{
    h->global = global;
    h->focus = FOCUS_SEARCH;
}

bool help_open(help_modal *h, help_context context, const char *area, bool global)
{
    if (!h->layout)
        h->layout = calloc(1, sizeof(*h->layout));
    if (!h->layout)
        return false;
    if (!h->matches)
        h->matches = calloc(G_N_ELEMENTS(rows), sizeof(*h->matches));
    if (!h->matches)
        return false;
    if (!h->open) {
        h->scroll[0] = h->scroll[1] = 0;
        h->context = context ? context : HELP_COMMON;
        h->area = area;
        text_clear(&h->search);
        h->search_first = 0;
        filter_rows(h);
    }
    switch_help(h, global);
    h->open = true;
    return true;
}
void help_destroy(help_modal *h)
{
    if (h->window)
        delwin(h->window);
    free(h->layout);
    free(h->matches);
    *h = (help_modal){0};
}
static void close_help(help_modal *h)
{
    h->open = false;
    if (h->window)
        delwin(h->window);
    h->window = NULL;
}
void help_paste(help_modal *h, text_paste *paste)
{
    if (h->open && h->focus == FOCUS_SEARCH) {
        for (size_t i = 0; i < paste->text.length; ++i)
            if (paste->text.data[i] == '\n' || paste->text.data[i] == '\r')
                paste->text.data[i] = ' ';
        if (text_paste_commit(paste, &h->search))
            filter_rows(h);
    } else
        text_paste_cancel(paste);
}
bool help_key(help_modal *h, int type, uint32_t key)
{
    if (!h->open)
        return false;
    if (shortcut_is(type, key, SHORTCUT_HELP) || shortcut_is(type, key, SHORTCUT_HELP_ALL)) {
        bool global = shortcut_is(type, key, SHORTCUT_HELP_ALL);
        if (global == h->global)
            close_help(h);
        else
            switch_help(h, global);
        return true;
    }
    if (type == OK && key == 27) {
        close_help(h);
        return true;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        h->focus = (h->focus + (type == KEY_CODE_YES ? 3 : 1)) % 4;
        return true;
    }
    if (shortcut_is(type, key, SHORTCUT_SEARCH)) {
        h->focus = FOCUS_SEARCH;
        return true;
    }
    if ((type == OK && (key == '\r' || key == '\n')) ||
        (type == KEY_CODE_YES && key == KEY_ENTER)) {
        if (h->focus == FOCUS_CLOSE)
            close_help(h);
        else if (h->focus == FOCUS_SWITCH)
            switch_help(h, !h->global);
        else
            h->focus = FOCUS_TABLE;
        return true;
    }
    bool scroll_key = type == KEY_CODE_YES &&
                      (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE);
    if (h->focus == FOCUS_SEARCH && !scroll_key) {
        text_buffer before = h->search;
        int width = h->window ? getmaxx(h->window) - 12 : 24;
        if (text_edit(&h->search, type, key, width, false)) {
            if (before.length != h->search.length ||
                memcmp(before.data, h->search.data, h->search.length * sizeof(*h->search.data)))
                filter_rows(h);
            return true;
        }
    }
    int *scroll = &h->scroll[h->global];
    if (type == KEY_CODE_YES) {
        int page = h->rows > 1 ? h->rows - 1 : 1;
        if (key == KEY_UP || key == KEY_PPAGE)
            *scroll -= key == KEY_UP ? 1 : page;
        else if (key == KEY_DOWN || key == KEY_NPAGE)
            *scroll += key == KEY_DOWN ? 1 : page;
        else if (key == KEY_HOME)
            *scroll = 0;
        else if (key == KEY_END)
            *scroll = h->total;
        else if (key == KEY_MOUSE && h->window) {
            MEVENT event;
            if (ui_mouse_get(&event) == OK) {
                int y = event.y, x = event.x;
                if (!wmouse_trafo(h->window, &y, &x, false)) {
                    if (event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                        close_help(h);
                } else if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
                    *scroll += event.bstate & BUTTON4_PRESSED ? -3 : 3;
                else if ((event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) &&
                         y == search_row(h) && x >= 10 && x < getmaxx(h->window) - 2) {
                    h->focus = FOCUS_SEARCH;
                    text_click(&h->search, TEXT_LIMIT * 2 + 1, 0, h->search_first + x - 10);
                } else if ((event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) &&
                           y == getmaxy(h->window) - 2) {
                    if (x >= h->close_x)
                        close_help(h);
                    else if (x >= h->switch_x)
                        switch_help(h, !h->global);
                } else if ((event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) &&
                           y >= search_row(h) + 2 && y < getmaxy(h->window) - 4)
                    h->focus = FOCUS_TABLE;
            }
        }
    }
    if (*scroll < 0)
        *scroll = 0;
    int last = h->total > h->rows ? h->total - h->rows : 0;
    if (*scroll > last)
        *scroll = last;
    return true; /* Never route help input to an underlying editor or action. */
}
static void table_rule(WINDOW *w, int y, int column, chtype left, chtype middle, chtype right)
{
    int width = getmaxx(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    mvwhline(w, y, 2, ACS_HLINE, width - 4);
    mvwaddch(w, y, 1, left);
    mvwaddch(w, y, column - 1, middle);
    mvwaddch(w, y, width - 2, right);
}

WINDOW *help_draw(help_modal *h)
{
    if (!h->open || COLS < 30 || LINES < 14)
        return NULL;
    int width = COLS - 4 < 98 ? COLS - 4 : 98;
    int height = LINES - 4 < 30 ? LINES - 4 : 30;
    if (height < 12)
        height = 12; /* Keep header, one wrapped row and table borders at 44x14. */
    int top = (LINES - height) / 2, left = (COLS - width) / 2;
    if (h->window && (getmaxx(h->window) != width || getmaxy(h->window) != height ||
                      getbegy(h->window) != top || getbegx(h->window) != left)) {
        delwin(h->window);
        h->window = NULL;
    }
    if (!h->window)
        h->window = newwin(height, width, top, left);
    if (!h->window)
        return NULL;
    WINDOW *w = h->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, h->global ? _("Ajuda global") : _("Ajuda do contexto"));
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!h->global)
        text_label(w, 2, 2, width - 4, _(h->area));
    int search = search_row(h);
    text_label(w, search, 2, 8, _("Buscar:"));
    int pair = h->focus == FOCUS_SEARCH ? THEME_FOCUSED : THEME_INPUT;
    wattrset(w, COLOR_PAIR(pair));
    mvwhline(w, search, 10, ' ', width - 12);
    text_draw_field(w, &h->search, &h->search_first, search, 10, width - 12, false);
    int cursor = getcurx(w);
    if (!h->search.length) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, search, 10, width - 12,
                   h->global ? _("teclas, função ou área") : _("teclas ou função"));
    }
    if (h->focus == FOCUS_SEARCH)
        mvwchgat(w, search, cursor, 1, A_REVERSE, pair, NULL);
    /* Table rows: top rule, header, header rule, then entries until height-4. */
    int table = search + 1, first_row = table + 3;
    int column = width >= 56 ? 26 : 24;
    if (column > width - 9)
        column = width - 9;
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, table + 1, 2, column - 3, column >= 23 ? _("Combinação de teclas") : _("Teclas"));
    text_label(w, table + 1, column + 1, width - column - 3, _("Função"));
    h->rows = height - 4 - first_row;
    if (h->rows < 1)
        h->rows = 1;
    int heights[sizeof(rows) / sizeof(*rows)] = {0};
    int total = 0;
    for (size_t i = 0; i < sizeof(rows) / sizeof(*rows); ++i) {
        if ((rows[i].contexts & HELP_VOICE) && !voice_available())
            continue;
        if (!h->matches[i] || (!h->global && !(rows[i].contexts & h->context)))
            continue;
        text_buffer text;
        if (!row_text(h, i, &text))
            continue;
        text_measure(&text, width - column - 3, h->layout);
        int count = h->layout->rows > 0 ? h->layout->rows : 1;
        text_decode(&text, _(rows[i].keys));
        text_measure(&text, column - 3, h->layout);
        if (h->layout->rows > count)
            count = h->layout->rows;
        heights[i] = count + 1;
        total += heights[i];
    }
    h->total = total;
    int last = total > h->rows ? total - h->rows : 0;
    if (h->scroll[h->global] > last)
        h->scroll[h->global] = last;
    int scroll = h->scroll[h->global];
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    mvwvline(w, table + 1, 1, ACS_VLINE, height - table - 5);
    mvwvline(w, table + 1, column - 1, ACS_VLINE, height - table - 5);
    mvwvline(w, table + 1, width - 2, ACS_VLINE, height - table - 5);
    total = 0;
    for (size_t i = 0; i < sizeof(rows) / sizeof(*rows); ++i) {
        if (!heights[i])
            continue;
        int count = heights[i] - 1;
        if (total + count > scroll && total < scroll + h->rows) {
            text_buffer text;
            if (!row_text(h, i, &text))
                continue;
            text_measure(&text, width - column - 3, h->layout);
            int first = scroll > total ? scroll - total : 0;
            int y = first_row + total + first - scroll;
            wattrset(w, COLOR_PAIR(THEME_PANEL));
            text_draw(w, &text, h->layout, first, h->rows - (y - first_row), y, column + 1);
            text_decode(&text, _(rows[i].keys));
            text_measure(&text, column - 3, h->layout);
            text_draw(w, &text, h->layout, first, h->rows - (y - first_row), y, 2);
        }
        int separator = first_row + total + count - scroll;
        if (separator >= first_row && separator < first_row + h->rows)
            table_rule(w, separator, column, ACS_LTEE, ACS_PLUS, ACS_RTEE);
        total += heights[i];
    }
    if (!total) {
        text_buffer empty;
        text_decode(&empty, _("Nenhum atalho encontrado."));
        text_measure(&empty, width - column - 3, h->layout);
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_draw(w, &empty, h->layout, 0, h->rows, first_row, column + 1);
    }
    table_rule(w, table, column, ACS_ULCORNER, ACS_TTEE, ACS_URCORNER);
    table_rule(w, table + 2, column, ACS_LTEE, ACS_PLUS, ACS_RTEE);
    table_rule(w, height - 4, column, ACS_LLCORNER, ACS_BTEE, ACS_LRCORNER);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 3, 2, width - 4, _("Ctrl+F: buscar · ↑↓: rolar · Esc: fechar"));
    h->switch_x = 2;
    h->close_x = width - 11;
    button_draw(w, height - 2, h->switch_x, width - 15,
                h->global ? _("[Ajuda do contexto]") : _("[Todos os atalhos]"),
                h->focus == FOCUS_SWITCH ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, height - 2, h->close_x, 9, _("[Fechar]"),
                h->focus == FOCUS_CLOSE ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
    return w;
}
