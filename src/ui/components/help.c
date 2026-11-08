#include "ui/components/help.h"
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
    {HELP_COMMON, "Ctrl+G / F1", "Abrir ou fechar a ajuda do contexto atual"},
    {HELP_COMMON, "Ctrl+/", "Abrir ou fechar a ajuda global"},
    {HELP_COMMON, "Ctrl+F na ajuda", "Focar a busca por teclas, função ou área"},
    {HELP_COMMON, "Tab / Shift+Tab na ajuda", "Percorrer busca, tabela e botões"},
    {HELP_COMMON, "Ctrl+Q", "Sair do aplicativo"},
    {HELP_CONVERSATION | HELP_LOGIN | HELP_ACCOUNT | HELP_PROFILES, "Tab / Shift+Tab",
     "Percorrer as áreas e os botões"},
    {HELP_CONVERSATION, "Alt+← / → / ↑ / ↓", "Ir à área ou ao botão naquela direção"},
    {HELP_CONVERSATION, "Ctrl+S", "Abrir configurações do aplicativo"},
    {HELP_CONVERSATION, "Ctrl+F", "Buscar mensagens no servidor"},
    {HELP_CONVERSATION, "Ctrl+B", "Abrir mensagens fixadas"},
    {HELP_CONVERSATION, "Ctrl+U", "Abrir ou fechar a lista de membros"},
    {HELP_CONVERSATION, "Ctrl+D", "Abrir a central de notificações"},
    {HELP_CONVERSATION | HELP_ACCOUNT, "Ctrl+T", "Abrir a conta; na conta, voltar às conversas"},
    {HELP_CONVERSATION | HELP_LOGIN | HELP_ACCOUNT, "Ctrl+P",
     "Selecionar ou gerenciar perfis de servidores"},
    {HELP_CONVERSATION, "Ctrl+X", "Voltar à mensagem do áudio em segundo plano e abrir o player"},
    {HELP_CONVERSATION, "Ctrl+L", "Sair da conta atual"},
    {HELP_CONVERSATION, "Ctrl+R", "Atualizar a conversa e permissões ou reconectar"},
    {HELP_SERVER_TABS, "← / →", "Selecionar um servidor ou Adicionar"},
    {HELP_SERVER_TABS, "Enter", "Ativar o servidor ou abrir a conexão de outro servidor"},
    {HELP_SERVER_TABS, "↓", "Entrar nos controles Canais/DMs"},
    {HELP_SERVER_TABS, "[+ Adicionar]", "Conectar outro servidor"},
    {HELP_SERVER_TABS, "Ctrl+Espaço / m / clique direito",
     "Abrir o menu do servidor, com Configurações do servidor e Remover servidor"},
    {HELP_SERVER_TABS, "Delete",
     "Remover o servidor da lista e apagar a sessão salva neste computador"},
    {HELP_HEADER | HELP_CHANNELS, "[Canais] / [DMs]",
     "Alternar a lista de canais e mensagens diretas"},
    {HELP_CHANNELS, "[⚙] / [*]", "Abrir as configurações do aplicativo"},
    {HELP_HEADER, "← / →", "Percorrer Canais/DMs e os botões do histórico"},
    {HELP_HEADER, "Enter", "Abrir o botão selecionado"},
    {HELP_HEADER, "↑ / ↓ / Esc", "Voltar aos servidores, à lista ou ao histórico"},
    {HELP_HEADER | HELP_HISTORY, "🔍 / 🖈 / 👤",
     "Clicar para abrir busca, mensagens fixadas ou membros"},
    {HELP_CHANNELS, "↑↓ / Home / End", "Escolher um canal ou DM; categorias são ignoradas"},
    {HELP_CHANNELS, "PgUp / PgDn", "Percorrer páginas da lista"},
    {HELP_CHANNELS, "Enter / →", "Abrir um canal de texto ou DM"},
    {HELP_CHANNELS | HELP_VOICE, "Enter em voz", "Conectar à chamada do canal de voz"},
    {HELP_CHANNELS | HELP_SETTINGS_BUTTON | HELP_VOICE, "🔊 / 🎤 / ♪ / ●",
     "Alternar áudio e microfone nos botões ao lado de Configurações"},
    {HELP_CHANNELS | HELP_VOICE, "[Sair da chamada]",
     "Encerrar a chamada; navegar preserva a conexão"},
    {HELP_CONVERSATION | HELP_VOICE, "Tecla configurada / segurar botão",
     "Transmitir com pressionar para falar; a combinação ou o botão 4/5 do mouse é "
     "escolhido em Configurações > Áudio"},
    {HELP_CHANNELS, "Ctrl+O / Shift+Enter",
     "Abrir o histórico do canal selecionado, inclusive voz"},
    {HELP_CHANNELS | HELP_MEMBERS, "Ctrl+Espaço / m", "Abrir o menu de contexto da seleção"},
    {HELP_CHANNELS | HELP_MEMBERS | HELP_HISTORY | HELP_SELECTION, "Clique direito",
     "Abrir o menu do canal, DM, membro ou mensagem"},
    {HELP_DM_LIST, "Ctrl+W", "Fechar a DM selecionada"},
    {HELP_CHANNELS, "↑ no início / Esc", "Voltar aos controles da lista ou ao histórico"},
    {HELP_HISTORY, "↑↓ / roda", "Rolar o histórico sem mudar de conversa"},
    {HELP_HISTORY, "Arrastar o mouse", "Selecionar texto do histórico e copiá-lo ao soltar"},
    {HELP_HISTORY, "PgUp / PgDn", "Rolar uma página e carregar mensagens ao chegar às bordas"},
    {HELP_HISTORY, "Home / End", "Ir ao início ou ao fim do histórico carregado"},
    {HELP_HISTORY | HELP_EDITOR | HELP_SELECTION, "Ctrl+K",
     "Entrar ou sair da seleção de mensagens"},
    {HELP_HISTORY, "Ctrl+Espaço", "Selecionar uma mensagem e abrir suas ações"},
    {HELP_HISTORY, "Digite texto", "Focar o editor e continuar o rascunho"},
    {HELP_HISTORY, "← / Esc", "Voltar à lista de canais"},
    {HELP_HISTORY, "↑ no início", "Focar os botões Busca, Fixadas e Membros"},
    {HELP_EDITOR, "Enter", "Enviar ou confirmar a edição; sem texto, perguntar se exclui"},
    {HELP_EDITOR, "Shift+Enter / Ctrl+N", "Inserir uma quebra de linha sem enviar"},
    {HELP_EDITOR | HELP_HISTORY, "Ctrl+E", "Abrir ou fechar o seletor de emojis"},
    {HELP_EDITOR | HELP_HISTORY, "Ctrl+O / 📎", "Escolher arquivos para anexar"},
    {HELP_EDITOR | HELP_HISTORY, "Ctrl+V",
     "Colar arquivos ou imagens do clipboard, quando disponível"},
    {HELP_EDITOR, "Ctrl+Y",
     "Tentar novamente o último envio com falha; envios incertos pedem confirmação"},
    {HELP_EDITOR, "Ctrl+C", "Limpar o campo de edição"},
    {HELP_EDITOR | HELP_FIELDS, "Ctrl+W / Ctrl+Backspace", "Apagar a palavra anterior"},
    {HELP_EDITOR | HELP_FIELDS, "Ctrl+← / Ctrl+→", "Mover o cursor por palavras"},
    {HELP_EDITOR | HELP_FIELDS, "Shift+setas", "Selecionar texto por caracteres ou linhas"},
    {HELP_EDITOR | HELP_FIELDS, "Ctrl+Shift+setas", "Selecionar texto por palavras"},
    {HELP_EDITOR | HELP_FIELDS, "Ctrl+Shift+C", "Copiar a seleção; sem seleção, copiar o campo"},
    {HELP_EDITOR | HELP_FIELDS, "Home / End", "Mover ao início ou ao fim da linha"},
    {HELP_EDITOR, "PgUp / PgDn", "Rolar o histórico uma linha mantendo o editor"},
    {HELP_EDITOR, "Shift+PgUp/PgDn", "Rolar uma página quando o terminal encaminha a combinação"},
    {HELP_EDITOR, "↑ com editor vazio", "Editar a última mensagem própria carregada"},
    {HELP_EDITOR, "@ / :nome", "Abrir sugestões de menções ou emojis"},
    {HELP_EDITOR, "/ no início", "Listar os comandos; ↑↓ escolhe, Tab completa, Esc fecha"},
    {HELP_EDITOR, "/nick nome", "Mudar seu apelido neste servidor; /nick sozinho o remove"},
    {HELP_EDITOR, "/status online|ausente|ocupado", "Mudar seu status neste servidor"},
    {HELP_EDITOR, "/bio texto", "Mudar a descrição do seu perfil; /bio sozinho a apaga"},
    {HELP_EDITOR, "/avatar / /banner", "Escolher a imagem do avatar ou do banner"},
    {HELP_EDITOR, "/banner remover", "Remover o banner do seu perfil"},
    {HELP_EDITOR, "//texto", "Enviar uma mensagem que começa com /"},
    {HELP_EDITOR, "↑↓ / Tab / Enter", "Escolher e inserir a sugestão quando a lista está aberta"},
    {HELP_EDITOR, "Esc", "Fechar sugestões, cancelar resposta/edição ou voltar ao histórico"},
    {HELP_EDITOR, "←→ nos anexos", "Escolher um anexo; Enter abre a remoção"},
    {HELP_SELECTION, "↑↓", "Escolher a mensagem ou carregar a próxima página"},
    {HELP_SELECTION, "Ctrl+Espaço / m", "Abrir as ações da mensagem selecionada"},
    {HELP_SELECTION, "r / e / E", "Responder, adicionar reação ou ver quem reagiu, se permitido"},
    {HELP_SELECTION, "←→ / Enter", "Escolher e abrir a mídia da mensagem"},
    {HELP_SELECTION, "Esc / Tab", "Sair da seleção e voltar ao foco anterior ou à próxima área"},
    {HELP_SETTINGS_BUTTON, "Enter / Espaço", "Abrir as configurações do aplicativo"},
    {HELP_SETTINGS_BUTTON, "↑ / ↓", "Focar a lista de canais ou o histórico"},
    {HELP_SETTINGS_BUTTON | HELP_EDITOR_BUTTONS, "Tab / Shift+Tab",
     "Focar a próxima área ou voltar"},
    {HELP_EDITOR_BUTTONS, "← / →", "Escolher um anexo ou um dos botões Anexar e Emojis"},
    {HELP_EDITOR_BUTTONS, "Enter", "Abrir o botão selecionado ou a confirmação de remover anexo"},
    {HELP_EDITOR_BUTTONS, "Ctrl+O / Ctrl+E", "Anexar arquivos ou abrir o seletor de emojis"},
    {HELP_EDITOR_BUTTONS | HELP_SETTINGS_BUTTON, "Esc", "Voltar ao editor ou ao histórico"},
    {HELP_MENU, "↑↓ / Enter / clique", "Escolher e executar uma opção disponível"},
    {HELP_MENU, "← / Esc", "Voltar do submenu ou fechar o menu"},
    {HELP_MENU, "Ctrl+Espaço / m", "Fechar o menu"},
    {HELP_MENU, "Ctrl+R", "Atualizar os dados do menu"},
    {HELP_MEMBERS, "↑↓ / Home / End", "Escolher um membro"},
    {HELP_MEMBERS, "PgUp / PgDn", "Percorrer páginas de membros"},
    {HELP_MEMBERS, "/ / Buscar", "Focar a busca por nome ou @username"},
    {HELP_MEMBERS, "Tab / Shift+Tab", "Percorrer busca, agrupamento e membros"},
    {HELP_MEMBERS, "Enter / clique", "Abrir o perfil ou alterar o agrupamento"},
    {HELP_MEMBERS, "Ctrl+R", "Atualizar a lista de membros"},
    {HELP_MEMBERS, "Esc / Ctrl+U", "Voltar do painel ou fechar a lista"},
    {HELP_MEMBER_SEARCH, "Enter / Tab", "Voltar à lista de membros ou ao agrupamento"},
    {HELP_MEMBER_SEARCH, "Esc / Ctrl+U", "Fechar a lista de membros"},
    {HELP_MEMBER_SEARCH, "Ctrl+R", "Atualizar os membros"},
    {HELP_MEMBER_PROFILE, "↑↓ / roda", "Rolar o perfil; botões iniciam DM ou ações permitidas"},
    {HELP_MEMBER_PROFILE | HELP_CONFIRM, "Tab / ←→ / Enter",
     "Escolher um botão e confirmar; ações destrutivas não são automáticas"},
    {HELP_MEMBER_PROFILE | HELP_CONFIRM, "Esc", "Cancelar e voltar sem executar a ação"},
    {HELP_EMOJIS, "Tab / Shift+Tab", "Percorrer busca, fontes e emojis"},
    {HELP_EMOJIS, "↑↓←→ / Enter", "Escolher e inserir um emoji ou reação"},
    {HELP_EMOJIS, "Ctrl+R", "Atualizar os emojis do servidor"},
    {HELP_EMOJIS, "Esc / Ctrl+E", "Fechar o seletor sem alterar o rascunho"},
    {HELP_NOTIFICATIONS, "↑↓ / roda", "Escolher uma notificação"},
    {HELP_NOTIFICATIONS, "Digite / Tab", "Buscar no conteúdo ou percorrer filtros e ações"},
    {HELP_NOTIFICATIONS, "←→ / Enter", "Mudar o filtro ou abrir os detalhes da notificação"},
    {HELP_NOTIFICATIONS, "[Abrir conversa]", "Ler a conversa e localizar a mensagem selecionada"},
    {HELP_NOTIFICATIONS, "[Preferências]", "Configurar avisos no desktop e a prévia"},
    {HELP_NOTIFICATIONS, "[Mais antigas]", "Carregar mais notificações"},
    {HELP_NOTIFICATIONS, "[Nome] / filtros", "Selecionar uma conversa ou ordenar por data"},
    {HELP_NOTIFICATIONS | HELP_SEARCH, "Ctrl+R / Esc",
     "Atualizar/tentar novamente ou voltar ao painel anterior"},
    {HELP_SEARCH, "Tab / Shift+Tab", "Percorrer consulta, filtros, busca e resultados"},
    {HELP_SEARCH, "↑↓ / PgUp / PgDn", "Percorrer resultados ou rolar os detalhes"},
    {HELP_SEARCH, "Enter / clique",
     "Buscar, abrir detalhes, localizar mensagem ou executar a ação selecionada"},
    {HELP_REACTIONS, "↑↓ / roda", "Rolar as reações ou as pessoas"},
    {HELP_REACTIONS, "←→ / Tab", "Alternar as colunas de reações e pessoas"},
    {HELP_REACTIONS, "Ctrl+R / Esc", "Atualizar a lista ou fechar o painel"},
    {HELP_FILES, "Tab / Shift+Tab", "Percorrer caminho, locais, arquivos e botões"},
    {HELP_FILES, "↑↓ / Enter", "Escolher um arquivo, abrir uma pasta ou confirmar"},
    {HELP_FILES, "Espaço", "Marcar ou desmarcar um arquivo"},
    {HELP_FILES, "Ctrl+V", "Colar arquivos ou imagens quando o clipboard está disponível"},
    {HELP_FILES, "Esc", "Cancelar a seleção de arquivos"},
    {HELP_SETTINGS, "Tab / ↑↓", "Percorrer as configurações e os modos Normal, Light e Ultralight"},
    {HELP_SETTINGS, "←→ / clique nas abas",
     "Trocar entre Perfil, Aparência, Áudio, Acessibilidade e Atalhos"},
    {HELP_SETTINGS, "Enter / clique", "Aplicar ou cancelar as configurações"},
    {HELP_SETTINGS, "Áudio: ↑↓ / ←→ / Enter",
     "Escolher dispositivos, ajustar volumes, testar o microfone e configurar pressionar para "
     "falar"},
    {HELP_SETTINGS, "Tecla para falar: Enter",
     "Capturar qualquer combinação ou os botões 4/5 do mouse; Esc cancela a captura"},
    {HELP_SETTINGS, "Perfil: ↑↓ / Enter",
     "Editar apelido, frases, descrição, status, avatar e banner deste servidor; Enter salva"},
    {HELP_SETTINGS, "Perfil: Shift+Enter", "Quebrar linha na descrição"},
    {HELP_SETTINGS, "Esc / Ctrl+S", "Fechar as configurações"},
    {HELP_SERVER_SETTINGS, "←→ nas abas / clique",
     "Trocar entre Visão geral, Cargos, Canais, Emojis, Membros e Auditoria; só aparecem as "
     "abas que suas permissões permitem"},
    {HELP_SERVER_SETTINGS, "Tab / Shift+Tab", "Percorrer abas, lista, formulário e Fechar"},
    {HELP_SERVER_SETTINGS, "↑↓ / PgUp / PgDn na lista",
     "Escolher um item; Enter abre o formulário, Carregar mais busca a próxima página"},
    {HELP_SERVER_SETTINGS, "Membros: digitar na lista", "Filtrar membros pelo nome"},
    {HELP_SERVER_SETTINGS, "Auditoria: ▾ Filtros",
     "Filtrar por entidade, ação, quem fez e datas (DD/MM/AAAA); Aplicar filtros recarrega"},
    {HELP_SERVER_SETTINGS, "↑↓ / Enter / Espaço no formulário",
     "Editar campos, marcar caixas e acionar botões; Enter num campo salva"},
    {HELP_SERVER_SETTINGS, "← / → numa opção", "Trocar o valor da opção"},
    {HELP_SERVER_SETTINGS, "Botões de exclusão", "Pressione duas vezes para confirmar"},
    {HELP_SERVER_SETTINGS, "Esc / [Fechar]", "Fechar as configurações do servidor"},
    {HELP_IMAGE, "← / →", "Trocar a imagem na galeria"},
    {HELP_MEDIA, "Ctrl+B / i / [Info]", "Mostrar ou ocultar as propriedades da mídia"},
    {HELP_MEDIA, "PgUp / PgDn / roda", "Rolar as propriedades quando abertas"},
    {HELP_MEDIA, "Esc", "Fechar a mídia e voltar ao histórico"},
    {HELP_MEDIA_LIST, "↑↓ / Enter / Esc", "Escolher e abrir uma mídia, ou voltar ao histórico"},
    {HELP_MEDIA_ERROR, "Enter", "Tentar carregar a mídia novamente"},
    {HELP_PLAYER, "Espaço", "Pausar ou continuar a reprodução"},
    {HELP_PLAYER, "← / →", "Retroceder ou avançar 5 segundos"},
    {HELP_PLAYER, ", / . ou < / >", "Retroceder ou avançar 1 segundo"},
    {HELP_PLAYER, "↑ / ↓", "Aumentar ou diminuir o volume"},
    {HELP_PLAYER, "m", "Silenciar ou reativar o som"},
    {HELP_PLAYER, "Clique / arraste", "Mover a posição ou o volume nas barras do player"},
    {HELP_AUDIO, "[ / ] ou [−] / [+]", "Diminuir ou aumentar a velocidade (0,5× a 2×)"},
    {HELP_AUDIO, "l / [Repetir]", "Repetir o áudio ao terminar"},
    {HELP_AUDIO, "Espaço no fim", "Recomeçar o áudio do início"},
    {HELP_AUDIO, "x / [Parar]", "Parar o áudio e fechar o player"},
    {HELP_AUDIO, "Esc / clique fora", "Fechar o player e continuar ouvindo em segundo plano"},
    {HELP_LOGIN, "Tab / ↑↓ / Enter",
     "Percorrer os campos e botões; Entrar autentica, Cadastro cria uma conta"},
    {HELP_LOGIN, "Ctrl+N", "Alternar Login/Cadastro quando desconectado"},
    {HELP_LOGIN | HELP_ACCOUNT, "Abas no topo / ← → / Enter",
     "Abrir outro servidor conectado; ↑ ou Shift+Tab a partir do primeiro campo chega às abas"},
    {HELP_LOGIN | HELP_ACCOUNT, "Clique direito / m nas abas",
     "Abrir o menu do servidor, com Remover servidor"},
    {HELP_LOGIN | HELP_ACCOUNT, "Delete nas abas",
     "Remover o servidor da lista e apagar a sessão salva; um servidor offline tenta "
     "a sessão salva de novo ao ser aberto"},
    {HELP_ACCOUNT, "Ctrl+R", "Renovar ou reconectar uma sessão existente"},
    {HELP_ACCOUNT, "[Canais]", "Voltar às conversas da sessão aberta"},
    {HELP_ACCOUNT, "[Sair da conta]", "Encerrar a sessão e desativar sua recuperação"},
    {HELP_LOGIN | HELP_ACCOUNT, "Esc", "Cancelar uma conexão ou voltar à sessão anterior"},
    {HELP_PROFILES, "↑↓ / ←→ / Tab", "Percorrer perfis, abas e botões"},
    {HELP_PROFILES, "Enter / [Selecionar]", "Usar o perfil e voltar ao login"},
    {HELP_PROFILES, "Ctrl+P / [Gerenciar]", "Abrir o gerenciador de perfis"},
    {HELP_PROFILES, "Ctrl+N / Ctrl+E", "Adicionar ou editar um perfil fora dos campos de edição"},
    {HELP_PROFILES, "Ctrl+D", "Abrir a confirmação de remoção do perfil"},
    {HELP_PROFILES, "Ctrl+L", "Configurar o orçamento de requisições"},
    {HELP_PROFILES, "Ctrl+Y / [Verificar]", "Identificar novamente o servidor selecionado"},
    {HELP_PROFILES, "Ctrl+R", "Recarregar os perfis salvos"},
    {HELP_PROFILES, "Esc / [Cancelar]", "Fechar um formulário ou voltar sem selecionar"},
    {HELP_FIELDS, "Ctrl+C", "Limpar o campo de texto"},
    {HELP_FIELD_CLEAR, "Ctrl+U", "Limpar o campo de login ou perfil"},
    {HELP_FIELDS, "Colar texto", "Inserir texto sem enviar o formulário"},
};

static const char *row_scope(help_context context)
{
    static const struct {
        help_context context;
        const char *name;
    } names[] = {{HELP_COMMON, "Aplicativo"},
                 {HELP_CONVERSATION, "Conversas"},
                 {HELP_LOGIN, "Conta/login"},
                 {HELP_PROFILES, "Perfis"},
                 {HELP_EDITOR, "Editor"},
                 {HELP_FIELDS, "Formulários"},
                 {HELP_HISTORY, "Histórico"},
                 {HELP_CHANNELS, "Canais/DMs"},
                 {HELP_MEMBERS, "Membros"},
                 {HELP_EMOJIS, "Emojis"},
                 {HELP_NOTIFICATIONS, "Notificações"},
                 {HELP_SEARCH, "Busca/Fixadas"},
                 {HELP_REACTIONS, "Reações"},
                 {HELP_FILES, "Arquivos"},
                 {HELP_SETTINGS, "Configurações"},
                 {HELP_SERVER_SETTINGS, "Configurações do servidor"},
                 {HELP_MENU, "Menu"},
                 {HELP_MEDIA, "Mídia"},
                 {HELP_AUDIO, "Áudio"},
                 {HELP_PLAYER, "Player"},
                 {HELP_CONFIRM, "Confirmação"},
                 {HELP_MEMBER_PROFILE, "Perfil de membro"},
                 {HELP_HEADER, "Botões"},
                 {HELP_SERVER_TABS, "Servidores"},
                 {HELP_SELECTION, "Seleção de mensagens"},
                 {HELP_EDITOR_BUTTONS, "Botões do editor"},
                 {HELP_SETTINGS_BUTTON, "Botão Configurações"},
                 {HELP_IMAGE, "Galeria"},
                 {HELP_MEDIA_LIST, "Lista de mídias"},
                 {HELP_MEDIA_ERROR, "Erro de mídia"},
                 {HELP_ACCOUNT, "Conta"},
                 {HELP_MEMBER_SEARCH, "Busca de membros"},
                 {HELP_FIELD_CLEAR, "Formulários"},
                 {HELP_DM_LIST, "Lista de DMs"}};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (context & names[i].context)
            return names[i].name;
    return "Navegação";
}
static bool row_text(const help_modal *h, size_t i, text_buffer *text)
{
    char description[512];
    snprintf(description, sizeof(description), "%s%s%s",
             h->global ? row_scope(rows[i].contexts) : "", h->global ? ": " : "", rows[i].function);
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
        snprintf(entry, sizeof(entry), "%s %s %s", rows[i].keys, row_scope(rows[i].contexts),
                 rows[i].function);
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
    text_label(w, 1, 2, width - 4, h->global ? "Ajuda global" : "Ajuda do contexto");
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!h->global)
        text_label(w, 2, 2, width - 4, h->area);
    int search = search_row(h);
    text_label(w, search, 2, 8, "Buscar:");
    int pair = h->focus == FOCUS_SEARCH ? THEME_FOCUSED : THEME_INPUT;
    wattrset(w, COLOR_PAIR(pair));
    mvwhline(w, search, 10, ' ', width - 12);
    text_draw_field(w, &h->search, &h->search_first, search, 10, width - 12, false);
    int cursor = getcurx(w);
    if (!h->search.length) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, search, 10, width - 12,
                   h->global ? "teclas, função ou área" : "teclas ou função");
    }
    if (h->focus == FOCUS_SEARCH)
        mvwchgat(w, search, cursor, 1, A_REVERSE, pair, NULL);
    /* Table rows: top rule, header, header rule, then entries until height-4. */
    int table = search + 1, first_row = table + 3;
    int column = width >= 56 ? 26 : 24;
    if (column > width - 9)
        column = width - 9;
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, table + 1, 2, column - 3, column >= 23 ? "Combinação de teclas" : "Teclas");
    text_label(w, table + 1, column + 1, width - column - 3, "Função");
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
        text_decode(&text, rows[i].keys);
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
            text_decode(&text, rows[i].keys);
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
        text_decode(&empty, "Nenhum atalho encontrado.");
        text_measure(&empty, width - column - 3, h->layout);
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_draw(w, &empty, h->layout, 0, h->rows, first_row, column + 1);
    }
    table_rule(w, table, column, ACS_ULCORNER, ACS_TTEE, ACS_URCORNER);
    table_rule(w, table + 2, column, ACS_LTEE, ACS_PLUS, ACS_RTEE);
    table_rule(w, height - 4, column, ACS_LLCORNER, ACS_BTEE, ACS_LRCORNER);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 3, 2, width - 4, "Ctrl+F: buscar · ↑↓: rolar · Esc: fechar");
    h->switch_x = 2;
    h->close_x = width - 11;
    button_draw(w, height - 2, h->switch_x, width - 15,
                h->global ? "[Ajuda do contexto]" : "[Todos os atalhos]",
                h->focus == FOCUS_SWITCH ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, height - 2, h->close_x, 9, "[Fechar]",
                h->focus == FOCUS_CLOSE ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
    return w;
}
