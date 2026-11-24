#include "model/text/markdown.h"
#include "ui/input/text.h"
#include "ui/media/message_blocks.h"
#include <assert.h>
#include <glib.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static markdown_text rendered;

static const char *render(const char *source)
{
    markdown_text_free(&rendered);
    assert(markdown_render(source, &rendered));
    assert(rendered.length == (size_t)g_utf8_strlen(rendered.text, -1));
    return rendered.text;
}

/* Style of the first scalar of needle in the last rendering. */
static unsigned char style_of(const char *needle)
{
    const char *at = strstr(rendered.text, needle);
    assert(at);
    return rendered.styles[g_utf8_pointer_to_offset(rendered.text, at)];
}

/* True when needle, in the last rendering, is one whole link: each of its
 * scalars is part of a link, and the scalars around it are not. */
static bool linked(const char *needle)
{
    const char *at = strstr(rendered.text, needle);
    assert(at);
    size_t first = (size_t)g_utf8_pointer_to_offset(rendered.text, at);
    size_t count = (size_t)g_utf8_strlen(needle, -1);
    for (size_t i = 0; i < count; ++i)
        if (!(rendered.styles[first + i] & MARKDOWN_LINK))
            return false;
    return (!first || !(rendered.styles[first - 1] & MARKDOWN_LINK)) &&
           (first + count == rendered.length || !(rendered.styles[first + count] & MARKDOWN_LINK));
}

static void expect(const char *source, const char *text)
{
    const char *got = render(source);
    if (strcmp(got, text)) {
        fprintf(stderr, "source: %s\nexpected: %s\ngot: %s\n", source, text, got);
        abort();
    }
}

static const char *name(void *context, const char *id)
{
    (void)context;
    (void)id;
    return "Álice";
}

int main(void)
{
    assert(setlocale(LC_ALL, ""));
    init_graphemebreak();

    /* Inline spans lose their markers and keep their styles. */
    expect("a **negrito** *itálico* ~~riscado~~ `x*y*`", "a negrito itálico riscado x*y*");
    assert(style_of("negrito") == MARKDOWN_BOLD);
    assert(style_of("itálico") == MARKDOWN_ITALIC);
    assert(style_of("riscado") == MARKDOWN_STRIKE);
    assert(style_of("x*y*") == MARKDOWN_CODE);
    assert(style_of("a ") == 0);
    expect("***ambos***", "ambos");
    assert(style_of("ambos") == (MARKDOWN_BOLD | MARKDOWN_ITALIC));

    /* Chat conventions: '_' is italic, '__' underlines, '___' does both, and
     * '**' stays bold; nesting adds up. */
    expect("_i_ __s__ ___si___ **n** __*si*__ __**sn**__ __a _si_ b__", "i s si n si sn a si b");
    assert(style_of("i ") == MARKDOWN_ITALIC);
    assert(style_of("s ") == MARKDOWN_UNDERLINE);
    assert(style_of("si ") == (MARKDOWN_UNDERLINE | MARKDOWN_ITALIC));
    assert(style_of("n ") == MARKDOWN_BOLD);
    assert(style_of("sn") == (MARKDOWN_UNDERLINE | MARKDOWN_BOLD));
    assert(style_of("a ") == MARKDOWN_UNDERLINE);
    assert(style_of("si b") == (MARKDOWN_UNDERLINE | MARKDOWN_ITALIC));
    assert(style_of(" b") == MARKDOWN_UNDERLINE);
    expect("snake_case_name e __init__.py", "snake_case_name e init.py");
    assert(style_of("snake") == 0 && style_of("init") == MARKDOWN_UNDERLINE);

    /* Only '~~' strikes through: a single '~' stays as written. */
    expect("~~riscado~~ e ~5 minutos~ e ~til~", "riscado e ~5 minutos~ e ~til~");
    assert(style_of("riscado") == MARKDOWN_STRIKE);
    assert(style_of("~5") == 0 && style_of("~til~") == 0);
    /* Strikethrough combines with the other styles, in either order. */
    expect("~~**a**~~ **~~b~~** ~~__c__~~ ~~***d***~~", "a b c d");
    assert(style_of("a ") == (MARKDOWN_STRIKE | MARKDOWN_BOLD));
    assert(style_of("b ") == (MARKDOWN_STRIKE | MARKDOWN_BOLD));
    assert(style_of("c ") == (MARKDOWN_STRIKE | MARKDOWN_UNDERLINE));
    assert(style_of("d") == (MARKDOWN_STRIKE | MARKDOWN_BOLD | MARKDOWN_ITALIC));
    expect("~sozinho~\n\n~outro~", "~sozinho~\n\n~outro~");

    /* Unmatched markers and escapes stay literal. */
    expect("2 * 3 = 6 e **aberto", "2 * 3 = 6 e **aberto");
    assert(style_of("**") == 0);
    expect("\\*não\\* é itálico", "*não* é itálico");
    assert(style_of("não") == 0);

    /* Every line break and blank line typed by the author is kept. */
    expect("um\ndois\n\n\ntrês", "um\ndois\n\n\ntrês");
    expect("linha  \ncom quebra", "linha\ncom quebra");

    /* Links show their destination unless the label already is it. */
    expect("[site](https://example.com) e https://example.org",
           "site (https://example.com) e https://example.org");
    assert(style_of("site") == MARKDOWN_LINK);
    assert(style_of("(https://example.com)") == MARKDOWN_DECOR);
    assert(style_of("https://example.org") == MARKDOWN_LINK);
    expect("veja www.example.com e <https://a.b/c>", "veja www.example.com e https://a.b/c");
    assert(style_of("www") == MARKDOWN_LINK);
    expect("![gato](https://example.com/g.png)", "gato (https://example.com/g.png)");

    /* A bare link runs to the next blank whatever its query and fragment
     * hold, here a text fragment with ':', '~', '%' and ','. */
    const char *fragment =
        "https://windowsforum.com/news/alleged-gta-6-xbox-executable-leak-no-playable-windows-"
        "port-and-malware-risks.453778/#:~:text=Rather%20than%20relying%20solely%20on%20slow%2C"
        "%20traditional,translates%20RDNA%202%20shaders%20to%20Vulkan%20SPIR-V";
    expect(fragment, fragment);
    assert(linked(fragment));
    expect("a http://localhost:3000/x?y=1&amp;z=%20#a:b e https://user@a.b:8080/c@d;e=f$g fim",
           "a http://localhost:3000/x?y=1&amp;z=%20#a:b e https://user@a.b:8080/c@d;e=f$g fim");
    assert(linked("http://localhost:3000/x?y=1&amp;z=%20#a:b"));
    assert(linked("https://user@a.b:8080/c@d;e=f$g"));
    assert(style_of("fim") == 0);
    /* Markers inside an address are part of it, not formatting. */
    expect("https://a.b/x*y*z https://a.b/__init__.py https://a.b/c~~d~~e https://a.b/~f~g",
           "https://a.b/x*y*z https://a.b/__init__.py https://a.b/c~~d~~e https://a.b/~f~g");
    assert(linked("https://a.b/x*y*z") && linked("https://a.b/__init__.py"));
    assert(linked("https://a.b/c~~d~~e") && linked("https://a.b/~f~g"));
    assert(style_of("y*z") == MARKDOWN_LINK && style_of("init") == MARKDOWN_LINK);
    assert(style_of("d~~e") == MARKDOWN_LINK);
    /* Punctuation after the address and brackets it did not open stay out,
     * as do the markers of a span around it. */
    expect("veja (https://a.b/c), https://pt.example.org/wiki/C_(linguagem). Fim: https://a.b/d!",
           "veja (https://a.b/c), https://pt.example.org/wiki/C_(linguagem). Fim: https://a.b/d!");
    assert(linked("https://a.b/c") && linked("https://pt.example.org/wiki/C_(linguagem)"));
    assert(linked("https://a.b/d"));
    expect("{https://a.b/y}) https://a.b/f(a)(b)) https://a.b/z)))",
           "{https://a.b/y}) https://a.b/f(a)(b)) https://a.b/z)))");
    assert(linked("https://a.b/y") && linked("https://a.b/f(a)(b)") && linked("https://a.b/z"));
    expect("**https://a.b/n** _https://a.b/i_ ~~https://a.b/s~~ ~https://a.b/t~ \"https://a.b/q\"",
           "https://a.b/n https://a.b/i https://a.b/s ~https://a.b/t~ \"https://a.b/q\"");
    assert(style_of("https://a.b/n") == (MARKDOWN_LINK | MARKDOWN_BOLD));
    assert(style_of("https://a.b/i") == (MARKDOWN_LINK | MARKDOWN_ITALIC));
    assert(style_of("https://a.b/s") == (MARKDOWN_LINK | MARKDOWN_STRIKE));
    assert(linked("https://a.b/n") && linked("https://a.b/t") && linked("https://a.b/q"));
    /* Only where a link can begin, and not in code or in a written link. */
    expect("xhttps://a.b a@www.c.d ftp://www.e.f https:// www. `https://g.h` e www.i.j/k?l=m%20n.",
           "xhttps://a.b a@www.c.d ftp://www.e.f https:// www. https://g.h e www.i.j/k?l=m%20n.");
    assert(style_of("xhttps") == 0 && style_of("www.c.d") == 0 && style_of("www.e.f") == 0);
    assert(style_of("https:// ") == 0 && style_of("www. ") == 0);
    assert(style_of("https://g.h") == MARKDOWN_CODE);
    assert(linked("www.i.j/k?l=m%20n"));
    expect("[https://a.b/x*y*](https://c.d/#:~:text=e%20f) e [g](https://h.i/#:~:text=j%20k)",
           "https://a.b/xy (https://c.d/#:~:text=e%20f) e g (https://h.i/#:~:text=j%20k)");
    assert(style_of("(https://c.d") == MARKDOWN_DECOR &&
           style_of("(https://h.i") == MARKDOWN_DECOR);
    expect("```\nhttps://a.b/x*y*\n```\n> https://a.b/q~~w~~e",
           "https://a.b/x*y*\n│ https://a.b/q~~w~~e");
    assert(markdown_code_block(style_of("https://a.b/x")) && linked("https://a.b/q~~w~~e"));

    /* A message that is a single link names the address to open. */
    char *only = markdown_only_link(fragment);
    assert(only && !strcmp(only, fragment));
    g_free(only);
    only = markdown_only_link(" \n<https://a.b/c?d=e>\n ");
    assert(only && !strcmp(only, "https://a.b/c?d=e"));
    g_free(only);
    only = markdown_only_link("www.example.com/a");
    assert(only && !strcmp(only, "https://www.example.com/a"));
    g_free(only);
    assert(!markdown_only_link("veja https://a.b") && !markdown_only_link("https://a.b."));
    assert(!markdown_only_link("https://a.b https://c.d") && !markdown_only_link("(https://a.b)"));
    assert(!markdown_only_link("[a](https://a.b)") && !markdown_only_link("ftp://a.b/c"));
    assert(!markdown_only_link("javascript:alert(1)") && !markdown_only_link("file:///etc/passwd"));
    assert(!markdown_only_link("https://a.b:porta/") && !markdown_only_link("https://[::1"));
    assert(!markdown_only_link("") && !markdown_only_link(NULL) && !markdown_only_link("\xff"));

    /* Blocks: headings, quotes, lists, fenced code and rules. */
    expect("# Título\ntexto", "Título\ntexto");
    assert(style_of("Título") == MARKDOWN_BOLD);
    expect("> citado\n> segue\n>\n> outro", "│ citado\n│ segue\n│ \n│ outro");
    assert(style_of("│") == MARKDOWN_DECOR);
    assert(style_of("citado") == 0);
    expect("> a\n> > b", "│ a\n│ │ b");
    expect("- um\n- dois\n  - três", "• um\n• dois\n  • três");
    assert(style_of("•") == MARKDOWN_DECOR);
    expect("3. três\n4. quatro", "3. três\n4. quatro");
    expect("1) a\n2) b", "1) a\n2) b");
    expect("- item\n  continua", "• item\n  continua");
    expect("antes\n```c\nint *p = **q;\n  recuo\n```\ndepois",
           "antes\nint *p = **q;\n  recuo\ndepois");
    assert(style_of("int") == (MARKDOWN_CODE | MARKDOWN_CODE_BLOCK));
    assert(style_of("  recuo") == (MARKDOWN_CODE | MARKDOWN_CODE_BLOCK));
    /* The end of every code line belongs to the block; the line before the
     * fence does not, nor does inline code. */
    assert(style_of("\n  recuo") == MARKDOWN_CODE_BLOCK);
    assert(style_of("\nint") == 0);
    assert(style_of("depois") == 0);
    expect("```\na\n\nb\n```", "a\n\nb");
    assert(style_of("\n\n") == MARKDOWN_CODE_BLOCK);
    expect("antes\n\n```\ncódigo\n```\n\ndepois", "antes\n\ncódigo\n\ndepois");
    expect("a\n\n***\n\nb", "a\n\n───\n\nb");
    /* Indentation is not a code block in chat. */
    expect("    recuado", "recuado");
    /* Raw HTML stays as written. */
    expect("<b>não</b>", "<b>não</b>");
    assert(style_of("<b>") == 0);

    /* Mention tokens pass through for the history to resolve. */
    const char *token = "@mention(<@00000000-0000-4000-8000-000000000001>)";
    char source[256];
    snprintf(source, sizeof(source), "**oi** %s!", token);
    snprintf(source + 128, 128, "oi %s!", token);
    expect(source, source + 128);

    /* Terminal controls never reach the renderer. */
    char *clean = text_sanitize("\x1b[31mvermelho\r\n\tfim\x07");
    assert(clean && !strcmp(clean, "[31mvermelho\n fim"));
    g_free(clean);
    assert(!text_sanitize("\xff"));

    /* History rows: markers gone, styles on the right cells, mentions kept. */
    json_t *m = json_pack("{s:s,s:s,s:s}", "id", "00000000-0000-4000-8000-000000000009", "content",
                          "", "created_at", "2026-10-12T12:00:00Z");
    char content[256];
    snprintf(content, sizeof(content), "**forte** %s `c`", token);
    json_object_set_new(m, "content", json_string(content));
    message_blocks blocks;
    assert(message_blocks_build_mentions(&blocks, m, "Autor", "", 60, false, name, NULL, NULL));
    bool found = false;
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j) {
            message_segment *s = &blocks.rows[i].segments[j];
            if (strcmp(s->key, "content"))
                continue;
            assert(!strcmp(s->text, "forte @Álice c"));
            assert(s->highlight && s->highlight_length == 14);
            assert(s->highlight[0] == MARKDOWN_BOLD && s->highlight[4] == MARKDOWN_BOLD);
            assert(s->highlight[5] == 0);
            assert(s->highlight[6] & MARKDOWN_MENTION);
            assert(s->highlight[13] == MARKDOWN_CODE);
            found = true;
        }
    assert(found);
    message_blocks_free(&blocks);

    /* Plain content keeps the ordinary label drawing. */
    json_object_set_new(m, "content", json_string("sem formatação"));
    assert(message_blocks_build(&blocks, m, "Autor", "", 60, false));
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j)
            if (!strcmp(blocks.rows[i].segments[j].key, "content"))
                assert(!blocks.rows[i].segments[j].highlight);
    message_blocks_free(&blocks);

    /* Markers around nothing are shown as written. */
    json_object_set_new(m, "content", json_string("```\n```"));
    assert(message_blocks_build(&blocks, m, "Autor", "", 60, false));
    found = false;
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j)
            found |= !strcmp(blocks.rows[i].segments[j].key, "content") &&
                     !strcmp(blocks.rows[i].segments[j].text, "```");
    assert(found);
    message_blocks_free(&blocks);

    /* A code block between plain lines: its rows, an empty line included,
     * form one box, inset by one cell on each side of the content width. */
    json_object_set_new(m, "content", json_string("antes\n```\nx = 1\n\ny\n```\ndepois"));
    assert(message_blocks_build(&blocks, m, "Autor", "", 60, false));
    char shape[16] = "";
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j) {
            message_segment *s = &blocks.rows[i].segments[j];
            if (strcmp(s->key, "content"))
                continue;
            assert(strlen(shape) + 1 < sizeof(shape));
            strcat(shape, blocks.rows[i].code ? "C" : "p");
            if (blocks.rows[i].code)
                assert(s->x == 1 && s->width == blocks.rows[i].segments[j].width);
        }
    assert(!strcmp(shape, "pCCCp"));
    message_blocks_free(&blocks);

    /* Styles stay aligned across decoder chunks and trimmed line ends. */
    GString *long_text = g_string_new("```\n");
    for (int i = 0; i < 700; ++i)
        g_string_append(long_text, "ab   \n");
    g_string_append(long_text, "```\n**fim**");
    json_object_set_new(m, "content", json_string(long_text->str));
    g_string_free(long_text, TRUE);
    assert(message_blocks_build(&blocks, m, "Autor", "", 60, false));
    size_t code = 0, bold = 0;
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j) {
            message_segment *s = &blocks.rows[i].segments[j];
            if (strncmp(s->key, "content", 7) || !*s->text)
                continue;
            assert(!strstr(s->text, "**") && s->highlight);
            unsigned char expected =
                strcmp(s->text, "fim") ? MARKDOWN_CODE | MARKDOWN_CODE_BLOCK : MARKDOWN_BOLD;
            /* Code block rows are boxes inset by one cell. */
            assert(blocks.rows[i].code == (expected != MARKDOWN_BOLD));
            assert(s->x == (blocks.rows[i].code ? 1 : 0));
            for (size_t k = 0; k < s->highlight_length; ++k)
                assert(s->highlight[k] == expected);
            if (expected == MARKDOWN_BOLD)
                ++bold;
            else
                code += s->highlight_length;
        }
    /* Chunk boundaries may split a line in two rows; no scalar is lost. */
    assert(code == 700 * 2 && bold == 1);
    message_blocks_free(&blocks);
    json_decref(m);
    markdown_text_free(&rendered);
    puts("markdown ok");
    return 0;
}
