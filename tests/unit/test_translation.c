#include "model/text/translation.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    language_choice choice;
    assert(language_choice_parse("pt", &choice) && choice == LANGUAGE_PORTUGUESE);
    assert(language_choice_parse("en", &choice) && choice == LANGUAGE_ENGLISH);
    assert(language_choice_parse("system", &choice) && choice == LANGUAGE_SYSTEM);
    assert(!language_choice_parse("EN", &choice) && !language_choice_parse(NULL, &choice));
    for (int i = LANGUAGE_SYSTEM; i <= LANGUAGE_ENGLISH; ++i)
        assert(language_choice_parse(language_choice_name((language_choice)i), &choice) &&
               choice == (language_choice)i);

    /* Portuguese is the source text: the msgid itself, never a copy. */
    const char *source = "Idioma: < %s >";
    language_set(LANGUAGE_PORTUGUESE);
    assert(!language_english() && _(source) == source);
    assert(!strcmp(language_choice_label(LANGUAGE_SYSTEM), "Sistema"));

    language_set(LANGUAGE_ENGLISH);
    assert(language_english());
    assert(!strcmp(_(source), "Language: < %s >"));
    assert(!strcmp(language_choice_label(LANGUAGE_SYSTEM), "System"));
    /* Each language keeps its own name in the other one. */
    assert(!strcmp(language_choice_label(LANGUAGE_PORTUGUESE), "Português"));
    assert(!strcmp(language_choice_label(LANGUAGE_ENGLISH), "English"));
    /* Unknown text, an unknown context and empty text fall back to the source. */
    const char *unknown = "Texto sem tradução";
    assert(_(unknown) == unknown);
    assert(C_("contexto inexistente", source) == source);
    assert(!strcmp(_(""), ""));
    assert(_(NULL) == NULL);
    /* Plurals without an entry keep the Portuguese rule for both counts. */
    assert(!strcmp(P_("%zu item", "%zu itens", 1), "%zu item"));
    assert(!strcmp(P_("%zu item", "%zu itens", 0), "%zu itens"));
    assert(!strcmp(P_("%zu item", "%zu itens", 2), "%zu itens"));

    /* System follows PAPO_LANGUAGE before the system's own list; CTest sets "pt". The
     * system preference is read once, so it is only checked here. */
    const char *forced = getenv("PAPO_LANGUAGE");
    language_set(LANGUAGE_SYSTEM);
    if (forced && !strcmp(forced, "pt"))
        assert(!language_english() && _(source) == source);
    else if (forced && !strcmp(forced, "en"))
        assert(language_english());
    return 0;
}
