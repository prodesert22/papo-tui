#include "ui/input/profile_commands.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

/* Matches "/name" followed by the end of the text or whitespace; returns the
 * text after it, or NULL. Command names ignore case. */
static const char *command(const char *text, const char *name)
{
    size_t length = strlen(name);
    if (g_ascii_strncasecmp(text, name, length))
        return NULL;
    char next = text[length];
    return !next || next == ' ' || next == '\t' || next == '\n' || next == '\r' ? text + length
                                                                                : NULL;
}
static void trimmed(char *out, size_t capacity, const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
        ++text;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                      text[length - 1] == '\n' || text[length - 1] == '\r'))
        --length;
    if (length >= capacity)
        length = capacity - 1;
    /* Never cut a UTF-8 sequence in half. */
    while (length && ((unsigned char)text[length] & 0xc0) == 0x80)
        --length;
    memcpy(out, text, length);
    out[length] = 0;
}
static bool one_of(const char *word, const char *const *choices)
{
    for (; *choices; ++choices)
        if (!g_ascii_strcasecmp(word, *choices))
            return true;
    return false;
}
void profile_command_parse(const char *text, profile_command *c)
{
    memset(c, 0, sizeof(*c));
    if (!text || text[0] != '/')
        return;
    if (text[1] == '/') {
        c->kind = PROFILE_COMMAND_LITERAL;
        return;
    }
    const char *rest;
    char word[64];
    if ((rest = command(text, "/nick"))) {
        c->kind = PROFILE_COMMAND_NICK;
        trimmed(c->argument, sizeof(c->argument), rest);
    } else if ((rest = command(text, "/bio"))) {
        c->kind = PROFILE_COMMAND_BIO;
        trimmed(c->argument, sizeof(c->argument), rest);
    } else if ((rest = command(text, "/status"))) {
        static const char *const online[] = {"online", "disponivel", "disponível", NULL};
        static const char *const away[] = {"ausente", "away", NULL};
        static const char *const busy[] = {"ocupado", "busy", NULL};
        trimmed(word, sizeof(word), rest);
        c->kind = PROFILE_COMMAND_STATUS;
        if (one_of(word, away))
            snprintf(c->status, sizeof(c->status), "away");
        else if (one_of(word, busy))
            snprintf(c->status, sizeof(c->status), "busy");
        else if (!one_of(word, online)) {
            c->kind = PROFILE_COMMAND_USAGE;
            c->usage = "Uso: /status online, ausente ou ocupado";
        }
    } else if ((rest = command(text, "/avatar"))) {
        trimmed(word, sizeof(word), rest);
        c->kind = *word ? PROFILE_COMMAND_USAGE : PROFILE_COMMAND_AVATAR;
        c->usage = *word ? "Uso: /avatar (abre a escolha da imagem)" : NULL;
    } else if ((rest = command(text, "/banner"))) {
        static const char *const remove[] = {"remover", "remove", NULL};
        trimmed(word, sizeof(word), rest);
        c->kind = !*word                 ? PROFILE_COMMAND_BANNER
                  : one_of(word, remove) ? PROFILE_COMMAND_BANNER_REMOVE
                                         : PROFILE_COMMAND_USAGE;
        if (c->kind == PROFILE_COMMAND_USAGE)
            c->usage = "Uso: /banner para escolher a imagem, ou /banner remover";
    }
}

static const char *const status_choices[] = {"online", "ausente", "ocupado", NULL};
static const char *const status_descriptions[] = {"Disponível", "Ausente", "Ocupado", NULL};
static const char *const banner_choices[] = {"remover", NULL};
static const char *const banner_descriptions[] = {"Remover o banner do perfil", NULL};
static const profile_command_info commands[] = {
    {"nick", "Alterar seu apelido neste servidor.", "novo_apelido",
     "Novo apelido; vazio remove o apelido", true, NULL, NULL},
    {"status", "Alterar seu status neste servidor.", "status", "online, ausente ou ocupado", false,
     status_choices, status_descriptions},
    {"bio", "Alterar a descrição do seu perfil neste servidor.", "texto",
     "Nova descrição; vazio a apaga", true, NULL, NULL},
    {"avatar", "Escolher a imagem do seu avatar neste servidor.", NULL, NULL, true, NULL, NULL},
    {"banner", "Escolher a imagem do seu banner neste servidor.", "remover",
     "Opcional: remove o banner em vez de escolher uma imagem", true, banner_choices,
     banner_descriptions},
};
const profile_command_info *profile_commands(size_t *count)
{
    *count = sizeof(commands) / sizeof(commands[0]);
    return commands;
}
