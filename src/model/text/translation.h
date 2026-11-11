#ifndef PAPO_TRANSLATION_H
#define PAPO_TRANSLATION_H
#include <stdbool.h>

/* Interface language. Source strings are Portuguese and double as message IDs;
 * the English catalogue is generated from po/en.po by tools/translations.py and
 * compiled in, so a change applies at the next redraw on every platform and
 * package without locating catalogue files at run time.
 *
 * Mark text with _("..."), C_("context", "...") when the same Portuguese words
 * need different English ones, P_("singular", "plural", n) for counts, and
 * N_()/NC_() where the text sits in a static table and is translated at use.
 * Remote content (names, messages, topics) is never passed through these. */
typedef enum { LANGUAGE_SYSTEM, LANGUAGE_PORTUGUESE, LANGUAGE_ENGLISH } language_choice;

/* Stored names: "system", "pt" and "en". */
const char *language_choice_name(language_choice choice);
bool language_choice_parse(const char *name, language_choice *choice);
/* Label of a choice in its own language, for the settings panel. */
const char *language_choice_label(language_choice choice);
void language_set(language_choice choice);
language_choice language_get(void);
/* The language in effect. System follows PAPO_LANGUAGE ("pt" or "en") when set,
 * then the system's language list: Portuguese when it comes first among the
 * two, English otherwise. */
bool language_english(void);

#if defined(__GNUC__) || defined(__clang__)
/* The result keeps the printf conversions of the argument, so format checks
 * still apply through a translation; tools/translations.py enforces it. */
#define TRANSLATION_FORMAT(index) __attribute__((format_arg(index)))
#else
#define TRANSLATION_FORMAT(index)
#endif
/* Borrowed: the catalogue entry or msgid itself, valid for the process. */
const char *translate(const char *context, const char *msgid) TRANSLATION_FORMAT(2);
const char *translate_plural(const char *context, const char *singular, const char *plural,
                             unsigned long count) TRANSLATION_FORMAT(2);

#define _(text) translate(NULL, (text))
#define C_(context, text) translate((context), (text))
#define P_(singular, plural, count) translate_plural(NULL, (singular), (plural), (count))
#define N_(text) text
#define NC_(context, text) text
#endif
