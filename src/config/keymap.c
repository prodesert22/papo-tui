#include "config/keymap.h"
#include "model/text/translation.h"

#include <glib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define SCOPE(name) (1UL << KEYMAP_SCOPE_##name)
#define CTRL(c) {KEY_BINDING_CHAR, (c), KEY_BINDING_CTRL}
#define LETTER(c) {KEY_BINDING_CHAR, (c), 0}
#define FUNCTION(n) {KEY_BINDING_KEY, KEY_BINDING_F1 + (n) - 1, 0}
#define SUPER(c) {KEY_BINDING_CHAR, (c), KEY_BINDING_SUPER}
#define TEXT_MODIFIERS (KEY_BINDING_CTRL | KEY_BINDING_ALT | KEY_BINDING_SUPER)

typedef struct {
    const char *id, *label;
    unsigned long scopes;
    unsigned flags;
    key_binding keys[KEYMAP_BINDINGS];
} action_definition;

/* Scopes where a text field can have focus. */
#define CONVERSATION_AREAS                                                                         \
    (SCOPE(CONVERSATION) | SCOPE(EDITOR) | SCOPE(HISTORY) | SCOPE(CHANNELS) | SCOPE(SERVER_TABS))
static const action_definition actions[KEYMAP_ACTIONS] = {
    [KEYMAP_QUIT] = {"app.quit",
                     N_("Sair do aplicativo"),
                     SCOPE(APP),
                     KEYMAP_REQUIRED | KEYMAP_TEXT_SAFE,
                     {CTRL('q')}},
    [KEYMAP_HELP] = {"app.help",
                     N_("Abrir ou fechar a ajuda do contexto atual"),
                     SCOPE(APP),
                     KEYMAP_REQUIRED | KEYMAP_TEXT_SAFE,
                     {CTRL('g'), FUNCTION(1)}},
    [KEYMAP_HELP_ALL] = {"app.help_all",
                         N_("Abrir ou fechar a ajuda global"),
                         SCOPE(APP),
                         KEYMAP_TEXT_SAFE,
                         {CTRL('/')}},
    [KEYMAP_REFRESH] = {"app.refresh",
                        N_("Atualizar, tentar novamente ou reconectar"),
                        SCOPE(CONVERSATION) | SCOPE(MENU) | SCOPE(MEMBERS) | SCOPE(REACTIONS) |
                            SCOPE(EMOJIS) | SCOPE(MENTIONS) | SCOPE(NOTIFICATIONS) | SCOPE(SEARCH) |
                            SCOPE(ACCOUNT) | SCOPE(PROFILES),
                        KEYMAP_TEXT_SAFE,
                        {CTRL('r')}},
    [KEYMAP_SETTINGS] = {"conversation.settings",
                         N_("Abrir ou fechar as configurações do aplicativo"),
                         SCOPE(CONVERSATION) | SCOPE(SETTINGS),
                         KEYMAP_REQUIRED | KEYMAP_TEXT_SAFE,
                         {CTRL('s')}},
    [KEYMAP_SWITCHER] = {"conversation.switcher",
                         N_("Ir para um canal ou DM de qualquer servidor pelo nome"),
                         SCOPE(CONVERSATION) | SCOPE(SWITCHER),
                         KEYMAP_TEXT_SAFE,
                         {CTRL('k')}},
    [KEYMAP_SEARCH] = {"conversation.search",
                       N_("Buscar mensagens no servidor; na ajuda, focar a busca"),
                       SCOPE(CONVERSATION) | SCOPE(HELP),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('f')}},
    [KEYMAP_PINNED] = {"conversation.pinned",
                       N_("Abrir mensagens fixadas"),
                       SCOPE(CONVERSATION),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('b')}},
    [KEYMAP_MEMBERS] = {"conversation.members",
                        N_("Abrir ou fechar a lista de membros"),
                        SCOPE(CONVERSATION),
                        KEYMAP_TEXT_SAFE,
                        {CTRL('u')}},
    [KEYMAP_NOTIFICATIONS] = {"conversation.notifications",
                              N_("Abrir a central de notificações"),
                              SCOPE(CONVERSATION),
                              KEYMAP_TEXT_SAFE,
                              {CTRL('d')}},
    [KEYMAP_ACCOUNT] = {"conversation.account",
                        N_("Abrir a conta; na conta, voltar às conversas"),
                        SCOPE(CONVERSATION) | SCOPE(LOGIN) | SCOPE(ACCOUNT),
                        KEYMAP_TEXT_SAFE,
                        {CTRL('t')}},
    [KEYMAP_PROFILES] = {"conversation.profiles",
                         N_("Selecionar ou gerenciar perfis de servidores"),
                         SCOPE(CONVERSATION) | SCOPE(LOGIN) | SCOPE(ACCOUNT) | SCOPE(PROFILES),
                         KEYMAP_TEXT_SAFE,
                         {CTRL('p')}},
    [KEYMAP_PLAYER] = {"conversation.player",
                       N_("Voltar à mensagem do áudio em segundo plano e abrir o player"),
                       SCOPE(CONVERSATION),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('x')}},
    [KEYMAP_LOGOUT] = {"conversation.logout",
                       N_("Sair da conta atual"),
                       SCOPE(CONVERSATION),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('l')}},
    [KEYMAP_RECONNECT] = {"conversation.reconnect",
                          N_("Reconectar agora quando a conexão cair"),
                          SCOPE(CONVERSATION),
                          KEYMAP_TEXT_SAFE,
                          {FUNCTION(5)}},
    [KEYMAP_EMOJIS] = {"conversation.emojis",
                       N_("Abrir ou fechar o seletor de emojis"),
                       SCOPE(EDITOR) | SCOPE(HISTORY) | SCOPE(EMOJIS),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('e')}},
    [KEYMAP_ATTACH] = {"conversation.attach",
                       N_("Escolher arquivos para anexar; na lista de canais, abrir o histórico do "
                          "canal selecionado"),
                       SCOPE(CONVERSATION),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('o')}},
    [KEYMAP_RETRY_SEND] = {"conversation.retry_send",
                           N_("Tentar novamente o último envio com falha"),
                           SCOPE(CONVERSATION),
                           KEYMAP_TEXT_SAFE,
                           {CTRL('y')}},
    [KEYMAP_SELECT] = {"conversation.select",
                       N_("Entrar ou sair da seleção de mensagens"),
                       SCOPE(EDITOR) | SCOPE(HISTORY) | SCOPE(SELECTION),
                       KEYMAP_TEXT_SAFE,
                       {CTRL('j')}},
    [KEYMAP_MENU] = {"conversation.menu",
                     N_("Abrir o menu de contexto da seleção"),
                     SCOPE(CHANNELS) | SCOPE(HISTORY) | SCOPE(SERVER_TABS) | SCOPE(SELECTION) |
                         SCOPE(MEMBERS) | SCOPE(MENU) | SCOPE(LOGIN),
                     KEYMAP_TEXT_SAFE,
                     {CTRL(' ')}},
    [KEYMAP_CLOSE_DM] =
        {"conversation.close_dm", N_("Fechar a DM selecionada"), SCOPE(DM_LIST), 0, {CTRL('w')}},
    [KEYMAP_ITEM_MENU] = {"lists.menu",
                          N_("Abrir o menu do canal, DM, servidor, membro ou mensagem selecionado"),
                          SCOPE(CHANNELS) | SCOPE(SERVER_TABS) | SCOPE(SELECTION) | SCOPE(MEMBERS) |
                              SCOPE(MENU) | SCOPE(LOGIN),
                          KEYMAP_CASELESS,
                          {LETTER('m')}},
    [KEYMAP_REPLY] = {"selection.reply",
                      N_("Responder à mensagem selecionada"),
                      SCOPE(SELECTION),
                      KEYMAP_CASELESS,
                      {LETTER('r')}},
    [KEYMAP_REACT] = {"selection.react",
                      N_("Adicionar reação à mensagem selecionada"),
                      SCOPE(SELECTION),
                      0,
                      {LETTER('e')}},
    [KEYMAP_REACTORS] = {"selection.reactors",
                         N_("Ver quem reagiu à mensagem selecionada"),
                         SCOPE(SELECTION),
                         0,
                         {LETTER('E')}},
    [KEYMAP_MEMBER_SEARCH] = {"members.search",
                              N_("Focar a busca por nome ou @username"),
                              SCOPE(MEMBERS),
                              0,
                              {LETTER('/')}},
    [KEYMAP_COPY_LINK] = {"members.copy_link",
                          N_("Copiar o link de redefinição de senha"),
                          SCOPE(MEMBERS),
                          0,
                          {LETTER('c')}},
    [KEYMAP_MEDIA_INFO] = {"media.info",
                           N_("Mostrar ou ocultar as propriedades da mídia"),
                           SCOPE(MEDIA),
                           0,
                           {CTRL('b'), LETTER('i')}},
    [KEYMAP_MUTE] =
        {"player.mute", N_("Silenciar ou reativar o som"), SCOPE(PLAYER), 0, {LETTER('m')}},
    [KEYMAP_SEEK_BACK] =
        {"player.back", N_("Retroceder 1 segundo"), SCOPE(PLAYER), 0, {LETTER(','), LETTER('<')}},
    [KEYMAP_SEEK_FORWARD] =
        {"player.forward", N_("Avançar 1 segundo"), SCOPE(PLAYER), 0, {LETTER('.'), LETTER('>')}},
    [KEYMAP_SLOWER] = {"audio.slower",
                       N_("Diminuir a velocidade do áudio"),
                       SCOPE(AUDIO_PLAYER),
                       0,
                       {LETTER('[')}},
    [KEYMAP_FASTER] = {"audio.faster",
                       N_("Aumentar a velocidade do áudio"),
                       SCOPE(AUDIO_PLAYER),
                       0,
                       {LETTER(']')}},
    [KEYMAP_LOOP] =
        {"audio.loop", N_("Repetir o áudio ao terminar"), SCOPE(AUDIO_PLAYER), 0, {LETTER('l')}},
    [KEYMAP_STOP] = {"audio.stop",
                     N_("Parar o áudio e fechar o player"),
                     SCOPE(AUDIO_PLAYER),
                     0,
                     {LETTER('x')}},
    [KEYMAP_MARK_ALL] = {"files.mark_all",
                         N_("Marcar ou desmarcar todos os anexos em Baixar anexos"),
                         SCOPE(FILES),
                         KEYMAP_CASELESS,
                         {LETTER('a')}},
    [KEYMAP_REGISTER] = {"login.register",
                         N_("Alternar Login/Cadastro quando desconectado"),
                         SCOPE(LOGIN),
                         KEYMAP_TEXT_SAFE,
                         {CTRL('n')}},
    [KEYMAP_PROFILE_ADD] =
        {"profiles.add", N_("Adicionar um perfil"), SCOPE(PROFILES), KEYMAP_TEXT_SAFE, {CTRL('n')}},
    [KEYMAP_PROFILE_EDIT] = {"profiles.edit",
                             N_("Editar o perfil selecionado"),
                             SCOPE(PROFILES),
                             KEYMAP_TEXT_SAFE,
                             {CTRL('e')}},
    [KEYMAP_PROFILE_REMOVE] = {"profiles.remove",
                               N_("Abrir a confirmação de remoção do perfil"),
                               SCOPE(PROFILES),
                               KEYMAP_TEXT_SAFE,
                               {CTRL('d')}},
    [KEYMAP_PROFILE_LIMITS] = {"profiles.limits",
                               N_("Configurar o orçamento de requisições"),
                               SCOPE(PROFILES),
                               KEYMAP_TEXT_SAFE,
                               {CTRL('l')}},
    [KEYMAP_PROFILE_VERIFY] = {"profiles.verify",
                               N_("Identificar novamente o servidor selecionado"),
                               SCOPE(PROFILES),
                               KEYMAP_TEXT_SAFE,
                               {CTRL('y')}},
};

static bool valid_action(keymap_action action)
{
    return action > KEYMAP_NONE && action < KEYMAP_ACTIONS;
}

const char *keymap_action_id(keymap_action action)
{
    return valid_action(action) ? actions[action].id : NULL;
}

keymap_action keymap_action_find(const char *id)
{
    if (id)
        for (int i = 0; i < KEYMAP_ACTIONS; ++i)
            if (!strcmp(actions[i].id, id))
                return (keymap_action)i;
    return KEYMAP_NONE;
}

const char *keymap_action_label(keymap_action action)
{
    return valid_action(action) ? _(actions[action].label) : "";
}

unsigned keymap_action_flags(keymap_action action)
{
    return valid_action(action) ? actions[action].flags : 0;
}

unsigned long keymap_action_scopes(keymap_action action)
{
    return valid_action(action) ? actions[action].scopes : 0;
}

static unsigned fold(unsigned code)
{
    return code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
}

/* US layout pairs, as the terminal protocols report a base key with Shift. */
static const char shift_pairs[] = "1!2@3#4$5%6^7&8*9(0)-_=+[{]}\\|;:'\",<.>/?`~";

void keymap_binding_normalize(key_binding *binding)
{
    if (!binding || binding->kind != KEY_BINDING_CHAR)
        return;
    unsigned code = binding->code;
    if (binding->modifiers & TEXT_MODIFIERS) {
        if (code >= 'A' && code <= 'Z') {
            binding->code = code - 'A' + 'a';
            binding->modifiers |= KEY_BINDING_SHIFT;
            return;
        }
        for (size_t i = 0; shift_pairs[i] && shift_pairs[i + 1]; i += 2)
            if ((unsigned char)shift_pairs[i + 1] == code) {
                binding->code = (unsigned char)shift_pairs[i];
                binding->modifiers |= KEY_BINDING_SHIFT;
                return;
            }
        return;
    }
    if (!(binding->modifiers & KEY_BINDING_SHIFT))
        return;
    binding->modifiers &= ~(unsigned)KEY_BINDING_SHIFT;
    if (code >= 'a' && code <= 'z') {
        binding->code = code - 'a' + 'A';
        return;
    }
    for (size_t i = 0; shift_pairs[i] && shift_pairs[i + 1]; i += 2)
        if ((unsigned char)shift_pairs[i] == code) {
            binding->code = (unsigned char)shift_pairs[i + 1];
            return;
        }
}

/* Keys that belong to navigation or text editing everywhere. */
static bool reserved(const key_binding *binding)
{
    unsigned modifiers = binding->modifiers;
    if (binding->kind == KEY_BINDING_KEY) {
        switch (binding->code) {
        case KEY_BINDING_ENTER:
        case KEY_BINDING_TAB:
        case KEY_BINDING_BACKSPACE:
            return modifiers == 0 || modifiers == KEY_BINDING_SHIFT ||
                   (binding->code == KEY_BINDING_BACKSPACE &&
                    (modifiers == KEY_BINDING_CTRL || modifiers == KEY_BINDING_SUPER));
        case KEY_BINDING_ESCAPE:
            return modifiers == 0;
        case KEY_BINDING_UP:
        case KEY_BINDING_DOWN:
        case KEY_BINDING_LEFT:
        case KEY_BINDING_RIGHT:
        case KEY_BINDING_HOME:
        case KEY_BINDING_END:
            /* Plain, Shift and Ctrl move or select; Alt+arrows move the focus. */
            return !(modifiers & ~(unsigned)(KEY_BINDING_SHIFT | KEY_BINDING_CTRL)) ||
                   (modifiers == KEY_BINDING_ALT && binding->code <= KEY_BINDING_RIGHT);
        case KEY_BINDING_PAGE_UP:
        case KEY_BINDING_PAGE_DOWN:
            return modifiers == 0 || modifiers == KEY_BINDING_SHIFT;
        case KEY_BINDING_DELETE:
            return modifiers == 0;
        default:
            return false;
        }
    }
    if (binding->kind != KEY_BINDING_CHAR)
        return false;
    unsigned code = binding->code;
    if (modifiers == KEY_BINDING_CTRL)
        /* Ctrl+H, I, M and [ are Backspace, Tab, Enter and Esc in the terminal;
         * Ctrl+A, C and V select all, clear and paste in every field. */
        return code == 'h' || code == 'i' || code == 'm' || code == '[' || code == 'a' ||
               code == 'c' || code == 'v';
    return modifiers == (KEY_BINDING_CTRL | KEY_BINDING_SHIFT) && code == 'c';
}

const char *keymap_binding_error(keymap_action action, const key_binding *binding)
{
    if (!valid_action(action) || !binding || !key_binding_well_formed(binding))
        return _("Combinação de teclas inválida.");
    if (binding->kind == KEY_BINDING_MOUSE)
        return _("Use uma tecla; os botões do mouse só servem para pressionar para falar.");
    key_binding key = *binding;
    keymap_binding_normalize(&key);
    if (reserved(&key))
        return _("Esta tecla é fixa: navega, confirma ou edita texto.");
    if (key.kind == KEY_BINDING_CHAR && !(key.modifiers & TEXT_MODIFIERS)) {
        if (actions[action].flags & KEYMAP_TEXT_SAFE)
            return _("Esta função age em campos de texto: use Ctrl, Alt ou F1–F12.");
        if (key.code == ' ')
            return _("Espaço sozinho é fixo: marca, pausa ou ativa botões.");
    }
    return NULL;
}

static unsigned definition_count(keymap_action action)
{
    unsigned count = 0;
    while (count < KEYMAP_BINDINGS && actions[action].keys[count].kind != KEY_BINDING_NONE)
        ++count;
    return count;
}

/* Command (super) alternatives offered on macOS after the Ctrl defaults, which
 * keep working in every terminal. Only terminals with the kitty keyboard
 * protocol report Command, such as kitty, Ghostty and WezTerm with
 * enable_kitty_keyboard; Terminal.app and iTerm2 keep it for their menus. These
 * letters are free in the default bindings of those three terminals and of
 * macOS, while Command+A, C, D, E, F, G, H, J, K, L, M, N, Q, R, T, V and W
 * already belong to one of them. */
static const struct {
    keymap_action action;
    key_binding key;
} apple_keys[] = {
    {KEYMAP_HELP_ALL, SUPER('/')}, {KEYMAP_SETTINGS, SUPER('s')},   {KEYMAP_PINNED, SUPER('b')},
    {KEYMAP_MEMBERS, SUPER('u')},  {KEYMAP_PROFILES, SUPER('p')},   {KEYMAP_PLAYER, SUPER('x')},
    {KEYMAP_ATTACH, SUPER('o')},   {KEYMAP_RETRY_SEND, SUPER('y')},
};

void keymap_table_platform_defaults(keymap_table *table, bool apple)
{
    memset(table, 0, sizeof(*table));
    for (int i = 0; i < KEYMAP_ACTIONS; ++i) {
        table->count[i] = definition_count((keymap_action)i);
        memcpy(table->keys[i], actions[i].keys, sizeof(table->keys[i]));
    }
    for (size_t i = 0; apple && i < sizeof(apple_keys) / sizeof(apple_keys[0]); ++i) {
        keymap_action action = apple_keys[i].action;
        if (table->count[action] < KEYMAP_BINDINGS)
            table->keys[action][table->count[action]++] = apple_keys[i].key;
    }
}

void keymap_table_defaults(keymap_table *table)
{
#ifdef __APPLE__
    keymap_table_platform_defaults(table, true);
#else
    keymap_table_platform_defaults(table, false);
#endif
}

void keymap_table_from_overrides(keymap_table *table, const keymap_overrides *overrides)
{
    keymap_table_defaults(table);
    for (size_t i = 0; overrides && i < overrides->count; ++i) {
        keymap_action action = keymap_action_find(overrides->items[i].id);
        if (action == KEYMAP_NONE)
            continue;
        memset(table->keys[action], 0, sizeof(table->keys[action]));
        table->count[action] = overrides->items[i].count;
        memcpy(table->keys[action], overrides->items[i].keys,
               sizeof(key_binding) * overrides->items[i].count);
    }
}

static bool same_keys(const key_binding *left, unsigned left_count, const key_binding *right,
                      unsigned right_count)
{
    if (left_count != right_count)
        return false;
    for (unsigned i = 0; i < left_count; ++i)
        if (!key_binding_equal(&left[i], &right[i]))
            return false;
    return true;
}

void keymap_table_to_overrides(const keymap_table *table, const keymap_overrides *previous,
                               keymap_overrides *overrides)
{
    keymap_overrides result;
    memset(&result, 0, sizeof(result));
    keymap_table defaults;
    keymap_table_defaults(&defaults);
    for (int i = 0; i < KEYMAP_ACTIONS && result.count < KEYMAP_OVERRIDES; ++i) {
        unsigned count = table->count[i] > KEYMAP_BINDINGS ? KEYMAP_BINDINGS : table->count[i];
        if (same_keys(table->keys[i], count, defaults.keys[i], defaults.count[i]))
            continue;
        keymap_override *item = &result.items[result.count++];
        snprintf(item->id, sizeof(item->id), "%s", actions[i].id);
        item->count = count;
        memcpy(item->keys, table->keys[i], sizeof(key_binding) * count);
    }
    for (size_t i = 0; previous && i < previous->count && result.count < KEYMAP_OVERRIDES; ++i)
        if (keymap_action_find(previous->items[i].id) == KEYMAP_NONE)
            result.items[result.count++] = previous->items[i];
    *overrides = result;
}

const char *keymap_action_group(keymap_action action)
{
    static const struct {
        keymap_action last;
        const char *name;
    } groups[KEYMAP_GROUPS] = {
        {KEYMAP_REFRESH, N_("Aplicativo")},
        {KEYMAP_CLOSE_DM, N_("Conversas")},
        {KEYMAP_REACTORS, N_("Listas e seleção")},
        {KEYMAP_COPY_LINK, N_("Membros")},
        {KEYMAP_STOP, N_("Mídia e player")},
        {KEYMAP_MARK_ALL, N_("Arquivos")},
        {KEYMAP_PROFILE_VERIFY, N_("Login e perfis")},
    };
    for (int i = 0; i < KEYMAP_GROUPS; ++i)
        if (action <= groups[i].last)
            return _(groups[i].name);
    return "";
}

static const struct {
    const char *keys, *function;
} fixed_keys[] = {
    {N_("↑↓←→ / PgUp / PgDn / Home / End"), N_("Navegar, rolar e escolher")},
    {N_("Enter"), N_("Confirmar, enviar ou abrir")},
    {N_("Esc"), N_("Fechar, cancelar ou voltar")},
    {N_("Tab / Shift+Tab"), N_("Percorrer as áreas e os botões")},
    {N_("Alt+← / → / ↑ / ↓"), N_("Ir à área ou ao botão naquela direção")},
    {N_("Espaço"), N_("Marcar, pausar ou ativar o item selecionado")},
    {N_("Delete"), N_("Remover o servidor ou o anexo selecionado")},
    {N_("Shift+Enter / Ctrl+N"), N_("Inserir uma quebra de linha sem enviar")},
    {N_("Ctrl+W / Ctrl+Backspace"), N_("Apagar a palavra anterior")},
#ifdef __APPLE__
    {N_("Cmd+Backspace"), N_("Apagar a palavra anterior")},
#endif
    {N_("Ctrl+A"), N_("Selecionar todo o texto da caixa")},
    {N_("Ctrl+C"), N_("Limpar o campo; fora de um campo, sair")},
    {N_("Ctrl+U"), N_("Limpar o campo de texto")},
    {N_("Ctrl+V"), N_("Colar arquivos ou imagens do clipboard, quando disponível")},
    {N_("Ctrl+Shift+C"), N_("Copiar a seleção; sem seleção, copiar o campo")},
    {N_("Ctrl+← / Ctrl+→"), N_("Mover o cursor por palavras")},
    {N_("Shift+setas"), N_("Selecionar texto por caracteres ou linhas")},
};

int keymap_fixed_count(void)
{
    return (int)(sizeof(fixed_keys) / sizeof(fixed_keys[0]));
}

void keymap_fixed(int index, const char **keys, const char **function)
{
    if (index < 0 || index >= keymap_fixed_count()) {
        *keys = *function = "";
        return;
    }
    *keys = _(fixed_keys[index].keys);
    *function = _(fixed_keys[index].function);
}

/* Each area's parent: it hears the commands of every area above it. */
static const keymap_scope parents[KEYMAP_SCOPES] = {
    [KEYMAP_SCOPE_APP] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_CONVERSATION] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_EDITOR] = KEYMAP_SCOPE_CONVERSATION,
    [KEYMAP_SCOPE_HISTORY] = KEYMAP_SCOPE_CONVERSATION,
    [KEYMAP_SCOPE_SELECTION] = KEYMAP_SCOPE_HISTORY,
    [KEYMAP_SCOPE_CHANNELS] = KEYMAP_SCOPE_CONVERSATION,
    [KEYMAP_SCOPE_DM_LIST] = KEYMAP_SCOPE_CHANNELS,
    [KEYMAP_SCOPE_SERVER_TABS] = KEYMAP_SCOPE_CONVERSATION,
    [KEYMAP_SCOPE_MEMBERS] = KEYMAP_SCOPE_CONVERSATION,
    [KEYMAP_SCOPE_MENU] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_REACTIONS] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_EMOJIS] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_MENTIONS] = KEYMAP_SCOPE_EDITOR,
    [KEYMAP_SCOPE_NOTIFICATIONS] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_SEARCH] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_MEDIA] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_PLAYER] = KEYMAP_SCOPE_MEDIA,
    [KEYMAP_SCOPE_AUDIO_PLAYER] = KEYMAP_SCOPE_PLAYER,
    [KEYMAP_SCOPE_FILES] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_SWITCHER] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_HELP] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_SETTINGS] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_LOGIN] = KEYMAP_SCOPE_APP,
    [KEYMAP_SCOPE_ACCOUNT] = KEYMAP_SCOPE_LOGIN,
    [KEYMAP_SCOPE_PROFILES] = KEYMAP_SCOPE_APP,
};

/* The areas where a command listening in `scopes` acts: those areas and every
 * area below them. */
static unsigned long active_scopes(unsigned long scopes)
{
    unsigned long active = 0;
    for (int scope = 0; scope < KEYMAP_SCOPES; ++scope)
        for (int at = scope;; at = parents[at]) {
            if (scopes & (1UL << at)) {
                active |= 1UL << scope;
                break;
            }
            if (at == KEYMAP_SCOPE_APP)
                break;
        }
    return active;
}

/* Editing keys that only belong to some areas, so commands elsewhere may use
 * them. */
#define FIELDS                                                                                     \
    (SCOPE(LOGIN) | SCOPE(PROFILES) | SCOPE(HELP) | SCOPE(SWITCHER) | SCOPE(EMOJIS) |              \
     SCOPE(NOTIFICATIONS) | SCOPE(SEARCH) | SCOPE(SETTINGS))
static const struct {
    key_binding key;
    unsigned long scopes;
    const char *label;
} scoped_keys[] = {
    {CTRL('u'), FIELDS, N_("Limpar o campo de texto")},
    {CTRL('w'), FIELDS | SCOPE(EDITOR) | SCOPE(MEMBERS), N_("Apagar a palavra anterior")},
    {CTRL('n'), SCOPE(CONVERSATION), N_("Inserir uma quebra de linha sem enviar")},
};

keymap_conflict keymap_find_conflict(const keymap_table *table, keymap_action action,
                                     const key_binding *key)
{
    keymap_conflict found = {KEYMAP_CONFLICT_NONE, KEYMAP_NONE, NULL};
    if (!table || !valid_action(action) || !key)
        return found;
    key_binding wanted = *key;
    keymap_binding_normalize(&wanted);
    unsigned long active = active_scopes(actions[action].scopes);
    for (size_t i = 0; i < sizeof(scoped_keys) / sizeof(scoped_keys[0]); ++i)
        if (key_binding_equal(&scoped_keys[i].key, &wanted) &&
            (active & active_scopes(scoped_keys[i].scopes))) {
            found.kind = KEYMAP_CONFLICT_FIXED;
            found.label = _(scoped_keys[i].label);
            return found;
        }
    for (int other = 0; other < KEYMAP_ACTIONS; ++other) {
        if (other == action || !(active & active_scopes(actions[other].scopes)))
            continue;
        for (unsigned i = 0; i < table->count[other]; ++i)
            if (key_binding_equal(&table->keys[other][i], &wanted) ||
                ((actions[other].flags & KEYMAP_CASELESS) && wanted.kind == KEY_BINDING_CHAR &&
                 !wanted.modifiers && !table->keys[other][i].modifiers &&
                 table->keys[other][i].kind == KEY_BINDING_CHAR &&
                 fold(table->keys[other][i].code) == fold(wanted.code))) {
                found.kind = KEYMAP_CONFLICT_ACTION;
                found.action = (keymap_action)other;
                found.label = keymap_action_label(found.action);
                return found;
            }
    }
    return found;
}

bool keymap_conversation_action(keymap_action action)
{
    return valid_action(action) &&
           (active_scopes(actions[action].scopes) & active_scopes(SCOPE(CONVERSATION)));
}

static bool identifier(const char *id)
{
    size_t length = id ? strlen(id) : 0;
    if (!length || length >= KEYMAP_ID_BYTES)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '_' ||
              id[i] == '.'))
            return false;
    return true;
}

bool keymap_overrides_decode(keymap_overrides *overrides, const json_t *object)
{
    memset(overrides, 0, sizeof(*overrides));
    if (!json_is_object(object) || json_object_size(object) > KEYMAP_OVERRIDES)
        return false;
    const char *id;
    json_t *value;
    json_object_foreach((json_t *)object, id, value)
    {
        if (!identifier(id) || !json_is_array(value) || json_array_size(value) > KEYMAP_BINDINGS)
            return false;
        keymap_action action = keymap_action_find(id);
        keymap_override *item = &overrides->items[overrides->count++];
        snprintf(item->id, sizeof(item->id), "%s", id);
        for (size_t i = 0; i < json_array_size(value); ++i) {
            const char *text = json_string_value(json_array_get(value, i));
            key_binding key;
            if (!text || !key_binding_parse(text, &key) || !key_binding_well_formed(&key))
                return false;
            keymap_binding_normalize(&key);
            if (action != KEYMAP_NONE && keymap_binding_error(action, &key))
                return false;
            for (unsigned j = 0; j < item->count; ++j)
                if (key_binding_equal(&item->keys[j], &key))
                    return false;
            item->keys[item->count++] = key;
        }
        if (action != KEYMAP_NONE && (actions[action].flags & KEYMAP_REQUIRED) && !item->count)
            return false;
    }
    return true;
}

json_t *keymap_overrides_encode(const keymap_overrides *overrides)
{
    json_t *object = json_object();
    for (size_t i = 0; object && overrides && i < overrides->count; ++i) {
        json_t *keys = json_array();
        if (!keys || json_object_set_new(object, overrides->items[i].id, keys) < 0) {
            json_decref(object);
            return NULL;
        }
        for (unsigned j = 0; j < overrides->items[i].count; ++j) {
            char text[KEY_BINDING_TEXT];
            if (!key_binding_format(&overrides->items[i].keys[j], text, sizeof(text)) ||
                json_array_append_new(keys, json_string(text)) < 0) {
                json_decref(object);
                return NULL;
            }
        }
    }
    return object;
}

/* Keys in effect, the texts expanded from them and the texts they replaced,
 * which callers may still hold. */
static GMutex lock;
static bool initialized;
static keymap_table current;
static atomic_uint_fast64_t revision = 1;
typedef struct {
    uint64_t revision;
    char *text;
} expansion;
static GHashTable *expansions;
static GPtrArray *retired;

static void ensure_initialized(void)
{
    if (!initialized) {
        keymap_table_defaults(&current);
        initialized = true;
    }
}

void keymap_apply(const keymap_table *table)
{
    g_mutex_lock(&lock);
    ensure_initialized();
    if (memcmp(&current, table, sizeof(current))) {
        current = *table;
        atomic_fetch_add(&revision, 1);
    }
    g_mutex_unlock(&lock);
}

void keymap_apply_overrides(const keymap_overrides *overrides)
{
    keymap_table table;
    keymap_table_from_overrides(&table, overrides);
    keymap_apply(&table);
}

void keymap_current(keymap_table *table)
{
    g_mutex_lock(&lock);
    ensure_initialized();
    *table = current;
    g_mutex_unlock(&lock);
}

uint64_t keymap_revision(void)
{
    return atomic_load(&revision);
}

unsigned keymap_keys(keymap_action action, key_binding keys[KEYMAP_BINDINGS])
{
    if (!valid_action(action))
        return 0;
    g_mutex_lock(&lock);
    ensure_initialized();
    unsigned count = current.count[action];
    memcpy(keys, current.keys[action], sizeof(current.keys[action]));
    g_mutex_unlock(&lock);
    return count;
}

bool keymap_matches(keymap_action action, const key_binding *key)
{
    if (!valid_action(action) || !key)
        return false;
    key_binding keys[KEYMAP_BINDINGS];
    unsigned count = keymap_keys(action, keys);
    bool caseless = actions[action].flags & KEYMAP_CASELESS;
    for (unsigned i = 0; i < count; ++i) {
        if (key_binding_equal(&keys[i], key))
            return true;
        if (caseless && keys[i].kind == KEY_BINDING_CHAR && key->kind == KEY_BINDING_CHAR &&
            !keys[i].modifiers && !key->modifiers && fold(keys[i].code) == fold(key->code))
            return true;
    }
    return false;
}

bool keymap_label(keymap_action action, bool first_only, char *output, size_t size)
{
    if (!output || !size)
        return false;
    output[0] = 0;
    key_binding keys[KEYMAP_BINDINGS];
    unsigned count = keymap_keys(action, keys);
    if (!count) {
        snprintf(output, size, "%s", _("sem tecla"));
        return valid_action(action);
    }
    size_t used = 0;
    for (unsigned i = 0; i < count && (!first_only || !i); ++i) {
        char label[64];
        if (!key_binding_label(&keys[i], label, sizeof(label)))
            return false;
        int written = snprintf(output + used, size - used, "%s%s", i ? " / " : "", label);
        if (written < 0 || (size_t)written >= size - used)
            return false;
        used += (size_t)written;
    }
    return true;
}

static char *expand(const char *text)
{
    GString *result = g_string_sized_new(strlen(text) + 32);
    bool format = strchr(text, '%') != NULL;
    for (const char *p = text; *p;) {
        bool first = !strncmp(p, "{k1:", 4);
        if (first || !strncmp(p, "{k:", 3)) {
            const char *id = p + (first ? 4 : 3), *end = strchr(id, '}');
            char name[KEYMAP_ID_BYTES];
            keymap_action action = KEYMAP_NONE;
            if (end && (size_t)(end - id) < sizeof(name)) {
                memcpy(name, id, (size_t)(end - id));
                name[end - id] = 0;
                action = keymap_action_find(name);
            }
            char label[256];
            if (action != KEYMAP_NONE && keymap_label(action, first, label, sizeof(label))) {
                for (const char *c = label; *c; ++c) {
                    if (format && *c == '%')
                        g_string_append_c(result, '%');
                    g_string_append_c(result, *c);
                }
                p = end + 1;
                continue;
            }
        }
        g_string_append_c(result, *p++);
    }
    return g_string_free(result, FALSE);
}

const char *keymap_text(const char *msgid)
{
    const char *text = _(msgid);
    if (!text || !strstr(text, "{k"))
        return text;
    uint64_t now = keymap_revision();
    g_mutex_lock(&lock);
    if (!expansions) {
        expansions = g_hash_table_new(g_direct_hash, g_direct_equal);
        retired = g_ptr_array_new_with_free_func(g_free);
    }
    expansion *entry = g_hash_table_lookup(expansions, text);
    if (!entry || entry->revision != now) {
        g_mutex_unlock(&lock);
        char *expanded = expand(text);
        g_mutex_lock(&lock);
        entry = g_hash_table_lookup(expansions, text);
        if (!entry) {
            entry = g_new0(expansion, 1);
            g_hash_table_insert(expansions, (gpointer)text, entry);
        } else if (entry->text)
            g_ptr_array_add(retired, entry->text);
        entry->text = expanded;
        entry->revision = now;
    }
    const char *result = entry->text;
    g_mutex_unlock(&lock);
    return result;
}

void keymap_shutdown(void)
{
    g_mutex_lock(&lock);
    if (expansions) {
        GHashTableIter iterator;
        gpointer value;
        g_hash_table_iter_init(&iterator, expansions);
        while (g_hash_table_iter_next(&iterator, NULL, &value)) {
            g_free(((expansion *)value)->text);
            g_free(value);
        }
        g_hash_table_destroy(expansions);
        g_ptr_array_free(retired, TRUE);
        expansions = NULL;
        retired = NULL;
    }
    g_mutex_unlock(&lock);
}
