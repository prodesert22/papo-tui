#include "model/text/translation.h"
#include <glib.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

typedef struct {
    const char *context; /* "" without context. */
    const char *msgid, *plural;
    const char *translation[2]; /* Singular and plural English forms. */
} translation_entry;
#include "model/text/translation_catalog.inc"

static atomic_int current = LANGUAGE_SYSTEM;
/* 0 unknown, 1 Portuguese, 2 English: the system preference, read once. */
static atomic_int system_language;

const char *language_choice_name(language_choice choice)
{
    return choice == LANGUAGE_PORTUGUESE ? "pt" : choice == LANGUAGE_ENGLISH ? "en" : "system";
}

bool language_choice_parse(const char *name, language_choice *choice)
{
    if (!name || !choice)
        return false;
    for (int i = LANGUAGE_SYSTEM; i <= LANGUAGE_ENGLISH; ++i)
        if (!strcmp(name, language_choice_name((language_choice)i))) {
            *choice = (language_choice)i;
            return true;
        }
    return false;
}

const char *language_choice_label(language_choice choice)
{
    /* Each language names itself, so it can be found whatever is in effect. */
    return choice == LANGUAGE_PORTUGUESE ? "Português"
           : choice == LANGUAGE_ENGLISH  ? "English"
                                         : _("Sistema");
}

void language_set(language_choice choice)
{
    if (choice >= LANGUAGE_SYSTEM && choice <= LANGUAGE_ENGLISH)
        atomic_store(&current, choice);
}

language_choice language_get(void)
{
    return (language_choice)atomic_load(&current);
}

static int language_of(const char *name)
{
    if (!g_ascii_strncasecmp(name, "pt", 2) && (!name[2] || strchr("_-.@", name[2])))
        return 1;
    if (!g_ascii_strncasecmp(name, "en", 2) && (!name[2] || strchr("_-.@", name[2])))
        return 2;
    return 0;
}

#ifdef __APPLE__
/* The first of Portuguese or English in a colon-separated list such as LANGUAGE; 0 for neither. */
static int listed_language(const char *list)
{
    int found = 0;
    gchar **names = g_strsplit(list, ":", -1);
    for (size_t i = 0; names[i] && !found; ++i)
        found = language_of(names[i]);
    g_strfreev(names);
    return found;
}
/* The languages chosen in System Settings, in their order. Terminals such as Ghostty put none of
 * them in LANG, which then holds only en_US.UTF-8 or nothing at all. */
static int preferred_language(void)
{
    CFArrayRef list = CFLocaleCopyPreferredLanguages();
    int found = 0;
    for (CFIndex i = 0; list && !found && i < CFArrayGetCount(list); ++i) {
        char name[64];
        CFStringRef item = CFArrayGetValueAtIndex(list, i);
        if (CFStringGetCString(item, name, sizeof(name), kCFStringEncodingUTF8))
            found = language_of(name);
    }
    if (list)
        CFRelease(list);
    return found;
}
#endif
static int detect_system(void)
{
    const char *forced = getenv("PAPO_LANGUAGE");
    if (forced && (!strcmp(forced, "pt") || !strcmp(forced, "en")))
        return forced[0] == 'p' ? 1 : 2;
#ifdef __APPLE__
    /* A language set on purpose in the environment still wins; LANG is the terminal's guess. */
    static const char *const chosen[] = {"LANGUAGE", "LC_ALL", "LC_MESSAGES"};
    for (size_t i = 0; i < G_N_ELEMENTS(chosen); ++i) {
        const char *value = getenv(chosen[i]);
        int found = value && *value ? listed_language(value) : 0;
        if (found)
            return found;
    }
    int preferred = preferred_language();
    if (preferred)
        return preferred;
#endif
    const char *const *names = g_get_language_names();
    for (size_t i = 0; names && names[i]; ++i) {
        int found = language_of(names[i]);
        if (found)
            return found;
    }
    return 2;
}

bool language_english(void)
{
    language_choice choice = language_get();
    if (choice != LANGUAGE_SYSTEM)
        return choice == LANGUAGE_ENGLISH;
    int known = atomic_load(&system_language);
    if (!known) {
        known = detect_system();
        atomic_store(&system_language, known);
    }
    return known == 2;
}

static const translation_entry *find(const char *context, const char *msgid)
{
    size_t low = 0, high = sizeof(translation_entries) / sizeof(translation_entries[0]);
    if (!context)
        context = "";
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const translation_entry *entry = &translation_entries[middle];
        int order = strcmp(context, entry->context);
        if (!order)
            order = strcmp(msgid, entry->msgid);
        if (!order)
            return entry;
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return NULL;
}

const char *translate(const char *context, const char *msgid)
{
    if (!msgid || !*msgid || !language_english())
        return msgid;
    const translation_entry *entry = find(context, msgid);
    return entry && !entry->plural ? entry->translation[0] : msgid;
}

const char *translate_plural(const char *context, const char *singular, const char *plural,
                             unsigned long count)
{
    /* Portuguese and English both use the singular for exactly one. */
    if (!singular || !plural)
        return count == 1 ? singular : plural;
    if (language_english()) {
        const translation_entry *entry = find(context, singular);
        if (entry && entry->plural && !strcmp(entry->plural, plural))
            return entry->translation[count == 1 ? 0 : 1];
    }
    return count == 1 ? singular : plural;
}
