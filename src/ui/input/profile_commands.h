#ifndef PAPO_PROFILE_COMMANDS_H
#define PAPO_PROFILE_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
/* Message editor commands that change the signed-in user's own profile.
 * Only these exact names are commands; any other text starting with "/" is an
 * ordinary message, and "//text" sends "/text". */
typedef enum {
    PROFILE_COMMAND_NONE,    /* Not a command: send the text as a message. */
    PROFILE_COMMAND_LITERAL, /* "//...": send the text without its first "/". */
    PROFILE_COMMAND_NICK,
    PROFILE_COMMAND_STATUS,
    PROFILE_COMMAND_BIO,
    PROFILE_COMMAND_AVATAR,
    PROFILE_COMMAND_BANNER,
    PROFILE_COMMAND_BANNER_REMOVE,
    PROFILE_COMMAND_USAGE /* A command with an invalid argument; keep the text. */
} profile_command_kind;
typedef struct {
    profile_command_kind kind;
    /* Trimmed argument, owned by the result (nick and bio), possibly empty. */
    char argument[2049 * 4];
    /* STATUS: "away", "busy" or "" (online). */
    char status[8];
    const char *usage; /* USAGE: static text for the footer. */
} profile_command;
void profile_command_parse(const char *text, profile_command *command);

/* What the editor's command chooser shows: each command, what it does and its
 * single option. choices, when present, are the accepted values in order,
 * with choice_descriptions beside them; both end with NULL. */
typedef struct {
    const char *name; /* Without the leading "/". */
    const char *description;
    const char *option; /* NULL when the command takes no option. */
    const char *option_description;
    bool optional; /* The command also runs without its option. */
    const char *const *choices;
    const char *const *choice_descriptions;
} profile_command_info;
const profile_command_info *profile_commands(size_t *count);
#endif
