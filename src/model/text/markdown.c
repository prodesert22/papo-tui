#include "model/text/markdown.h"

#include <glib.h>
#include <md4c.h>
#include <stdio.h>
#include <string.h>

/* Containers deeper than this still render their text, without a prefix. */
#define MARKDOWN_DEPTH 32
/* Bounded output: a pathological message falls back to its plain text. */
#define MARKDOWN_OUTPUT_LIMIT (1U << 20)

/* Symbols that the Linux console font and the text-symbol modes also show. */
#define QUOTE_MARK "│ "
#define BULLET_MARK "• "
#define RULE_MARK "───"

typedef struct {
    bool quote;
    bool pending; /* The item's bullet is still to be drawn on its first line. */
    char bullet[24];
    int width;
} container;

typedef struct {
    bool ordered;
    unsigned next;
    char delimiter;
} list;

typedef struct {
    size_t start; /* Byte offset in the output where the link text begins. */
    char *target; /* Owned; NULL when the target is not shown. */
} link;

/* Characters that end a sentence or close a Markdown span around a bare link
 * rather than belong to its address. */
#define LINK_TRAILING "?!.,:;*_~'"

typedef struct {
    const char *source;
    size_t size;
    GString *text;
    GByteArray *styles;
    size_t source_end; /* Byte after the last source text written out. */
    bool written;      /* Something visible was written. */
    unsigned since;    /* Newlines written since the last source text. */
    bool line_start, need_break, failed;
    container containers[MARKDOWN_DEPTH];
    int depth;
    list lists[MARKDOWN_DEPTH];
    int list_depth;
    link links[MARKDOWN_DEPTH];
    int link_depth;
    int strong, em, del, code, heading, code_block;
    int underscores; /* Nested '_' spans, one for each '_' of a delimiter run. */
    int tildes;      /* Single '~' spans whose opening '~' is still to be written. */
    /* The bare link being written: its bytes of source up to bare_at are
     * out, and those up to bare_end follow. Both are 0 outside one. */
    size_t bare_at, bare_end;
    unsigned char bare_style;
} renderer;

size_t markdown_link_length(const char *text, size_t size, size_t at)
{
    static const char *const prefixes[] = {"https://", "http://", "www."};
    size_t prefix = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(prefixes) && !prefix; ++i) {
        size_t length = strlen(prefixes[i]);
        if (at < size && size - at > length && !g_ascii_strncasecmp(text + at, prefixes[i], length))
            prefix = length;
    }
    if (!prefix)
        return 0;
    bool www = prefix == 4;
    /* A link begins where a scheme or a host name can: not inside a word,
     * another scheme or, for "www.", a longer host name or address. */
    unsigned char before = at ? (unsigned char)text[at - 1] : ' ';
    if (g_ascii_isalnum(before) ||
        (www ? before >= 0x80 || strchr("/.-_@:", before) : strchr("+-.", before) != NULL))
        return 0;
    unsigned char host = (unsigned char)text[at + prefix];
    if (!g_ascii_isalnum(host) && host < 0x80 && (www || host != '['))
        return 0;
    /* The address runs to the next blank, control or quoting character. */
    const char *start = text + at, *end = start + prefix, *stop = text + size;
    while (end < stop) {
        gunichar c = g_utf8_get_char(end);
        GUnicodeType kind = g_unichar_type(c);
        if (!g_unichar_isgraph(c) || (c < 0x80 && strchr("<>\"`\\", (int)c)) ||
            kind == G_UNICODE_INITIAL_PUNCTUATION || kind == G_UNICODE_FINAL_PUNCTUATION)
            break;
        end = g_utf8_next_char(end);
    }
    /* Punctuation after the address is the sentence's, and so is a closing
     * bracket that the address did not open. */
    static const char openers[] = "([{", closers[] = ")]}";
    int depth[3] = {0};
    for (const char *p = start; p < end; ++p) {
        const char *opener = strchr(openers, *p), *closer = strchr(closers, *p);
        if (opener)
            ++depth[opener - openers];
        else if (closer)
            --depth[closer - closers];
    }
    while (end > start + prefix + 1) {
        const char *closer = strchr(closers, end[-1]);
        if (closer && depth[closer - closers] < 0)
            ++depth[closer - closers];
        else if (closer || !strchr(LINK_TRAILING, end[-1]))
            break;
        --end;
    }
    return (size_t)(end - start);
}

static unsigned char current_style(const renderer *r)
{
    unsigned char style = 0;
    if (r->strong || r->heading)
        style |= MARKDOWN_BOLD;
    /* As in chat clients, '_' and '*' make italics, '__' underlines and '___'
     * does both: an odd number of '_' around the text adds italics, two or
     * more add the underline. */
    if (r->em || r->underscores % 2)
        style |= MARKDOWN_ITALIC;
    if (r->underscores >= 2)
        style |= MARKDOWN_UNDERLINE;
    if (r->del)
        style |= MARKDOWN_STRIKE;
    if (r->code || r->code_block)
        style |= MARKDOWN_CODE;
    if (r->code_block)
        style = MARKDOWN_CODE_BLOCK;
    if (r->link_depth)
        style |= MARKDOWN_LINK;
    return style;
}

static void raw(renderer *r, const char *bytes, size_t size, unsigned char style)
{
    if (r->failed || !size)
        return;
    if (r->text->len + size > MARKDOWN_OUTPUT_LIMIT) {
        r->failed = true;
        return;
    }
    g_string_append_len(r->text, bytes, (gssize)size);
    /* One style per scalar: count the lead bytes of the valid UTF-8 input. */
    for (size_t i = 0; i < size; ++i)
        if (((unsigned char)bytes[i] & 0xC0) != 0x80)
            g_byte_array_append(r->styles, &style, 1);
}

static void prefixes(renderer *r, bool blank)
{
    int depth = r->depth < MARKDOWN_DEPTH ? r->depth : MARKDOWN_DEPTH;
    for (int i = 0; i < depth; ++i) {
        container *c = &r->containers[i];
        if (c->quote)
            raw(r, QUOTE_MARK, strlen(QUOTE_MARK), MARKDOWN_DECOR);
        else if (blank)
            continue;
        else if (c->pending) {
            raw(r, c->bullet, strlen(c->bullet), MARKDOWN_DECOR);
            c->pending = false;
        } else
            for (int k = 0; k < c->width; ++k)
                raw(r, " ", 1, 0);
    }
}

static void newline(renderer *r, unsigned char style)
{
    /* A blank line inside a quote keeps its bar. */
    if (r->line_start)
        prefixes(r, true);
    raw(r, "\n", 1, style);
    r->line_start = true;
    ++r->since;
}

/* Visible text: the prefixes of the containers come first on each line. */
static void emit(renderer *r, const char *bytes, size_t size, unsigned char style)
{
    if (!size)
        return;
    if (r->line_start)
        prefixes(r, false);
    raw(r, bytes, size, style);
    r->line_start = false;
    r->written = true;
}

/* Lines between two source texts that hold only whitespace or quote marks:
 * the blank lines the author wrote between blocks. Lines with fences or
 * rules are not blank, because their markers are not shown. */
static unsigned blank_lines(const renderer *r, const char *at)
{
    if (at < r->source || at > r->source + r->size || r->source_end > (size_t)(at - r->source))
        return 0;
    unsigned count = 0;
    bool inside = false, blank = true;
    for (const char *p = r->source + r->source_end; p < at; ++p) {
        if (*p == '\n') {
            if (inside && blank)
                ++count;
            inside = true;
            blank = true;
        } else if (*p != ' ' && *p != '>')
            blank = false;
    }
    return count;
}

/* Separates a new block from the previous one with the newlines the author
 * wrote, and at least one. */
static void separate(renderer *r, const char *at)
{
    if (!r->need_break)
        return;
    r->need_break = false;
    if (!r->written)
        return;
    unsigned wanted = 1 + (at ? blank_lines(r, at) : 0);
    while (r->since < wanted && !r->failed)
        newline(r, 0);
}

static const char *rule_line(const renderer *r)
{
    const char *end = r->source + r->size;
    const char *p = r->source + r->source_end;
    /* Before any text the rule may start the message. */
    bool line = !r->written;
    for (; p < end; ++p) {
        if (*p == '\n') {
            line = true;
            continue;
        }
        if (line && *p != ' ' && *p != '>') {
            while (p > r->source && p[-1] != '\n')
                --p;
            return p;
        }
    }
    return NULL;
}

static void consumed(renderer *r, const char *text, size_t size)
{
    if (text < r->source || text + size > r->source + r->size)
        return;
    size_t end = (size_t)(text - r->source) + size;
    /* A code line's own newline stays in the gap, like any line end. */
    if (size && text[size - 1] == '\n')
        --end;
    if (end > r->source_end)
        r->source_end = end;
}

/* Ends the bare link being written. md4c consumes the Markdown markers that
 * an address contains, so what it left out up to the link's end is written
 * here, as the author typed it. */
static void finish_bare(renderer *r)
{
    if (r->bare_end > r->bare_at) {
        emit(r, r->source + r->bare_at, r->bare_end - r->bare_at, r->bare_style);
        r->since = 0;
        /* A '~' that opened a span in there is part of what was written. */
        r->tildes = 0;
        if (r->bare_end > r->source_end)
            r->source_end = r->bare_end;
    }
    r->bare_at = r->bare_end = 0;
}

static void push_container(renderer *r, bool quote, const char *bullet)
{
    if (r->depth < MARKDOWN_DEPTH) {
        container *c = &r->containers[r->depth];
        memset(c, 0, sizeof(*c));
        c->quote = quote;
        if (bullet) {
            snprintf(c->bullet, sizeof(c->bullet), "%s", bullet);
            c->width = (int)g_utf8_strlen(c->bullet, -1);
            c->pending = true;
        }
    }
    ++r->depth;
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    renderer *r = userdata;
    finish_bare(r);
    if (type == MD_BLOCK_DOC)
        return 0;
    r->need_break = true;
    switch (type) {
    case MD_BLOCK_QUOTE:
        push_container(r, true, NULL);
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (r->list_depth < MARKDOWN_DEPTH) {
            list *l = &r->lists[r->list_depth];
            l->ordered = type == MD_BLOCK_OL;
            l->next = l->ordered ? ((MD_BLOCK_OL_DETAIL *)detail)->start : 0;
            l->delimiter = l->ordered ? ((MD_BLOCK_OL_DETAIL *)detail)->mark_delimiter : 0;
        }
        ++r->list_depth;
        break;
    case MD_BLOCK_LI: {
        char bullet[24] = BULLET_MARK;
        if (r->list_depth > 0 && r->list_depth <= MARKDOWN_DEPTH) {
            list *l = &r->lists[r->list_depth - 1];
            if (l->ordered)
                snprintf(bullet, sizeof(bullet), "%u%c ", l->next++,
                         l->delimiter == ')' ? ')' : '.');
        }
        push_container(r, false, bullet);
        break;
    }
    case MD_BLOCK_H:
        ++r->heading;
        break;
    case MD_BLOCK_CODE:
        ++r->code_block;
        break;
    case MD_BLOCK_HR: {
        /* A rule has no text: its line is the first one after the previous
         * text that is not blank, and it counts as consumed source. */
        const char *line = rule_line(r);
        separate(r, line);
        emit(r, RULE_MARK, strlen(RULE_MARK), MARKDOWN_DECOR);
        r->since = 0;
        if (line) {
            const char *stop = memchr(line, '\n', r->size - (size_t)(line - r->source));
            r->source_end = stop ? (size_t)(stop - r->source) : r->size;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    (void)detail;
    renderer *r = userdata;
    finish_bare(r);
    if (type == MD_BLOCK_DOC)
        return 0;
    r->need_break = true;
    switch (type) {
    case MD_BLOCK_QUOTE:
    case MD_BLOCK_LI:
        if (r->depth > 0)
            --r->depth;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (r->list_depth > 0)
            --r->list_depth;
        break;
    case MD_BLOCK_H:
        --r->heading;
        break;
    case MD_BLOCK_CODE:
        --r->code_block;
        break;
    default:
        break;
    }
    return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    renderer *r = userdata;
    switch (type) {
    case MD_SPAN_STRONG:
        ++r->strong;
        break;
    case MD_SPAN_EM:
        ++r->em;
        break;
    case MD_SPAN_DEL:
        ++r->del;
        break;
    case MD_SPAN_U:
        ++r->underscores;
        break;
#ifdef MD_FLAG_SUBSCRIPTS
    case MD_SPAN_SUBSCRIPT:
        /* Only '~~' strikes text through; a single '~' stays as written. Its
         * opening mark waits for the span's text, which separates blocks. */
        ++r->tildes;
        break;
#endif
    case MD_SPAN_CODE:
        ++r->code;
        break;
    case MD_SPAN_A:
    case MD_SPAN_IMG: {
        const MD_ATTRIBUTE *target = type == MD_SPAN_A ? &((MD_SPAN_A_DETAIL *)detail)->href
                                                       : &((MD_SPAN_IMG_DETAIL *)detail)->src;
        if (r->link_depth < MARKDOWN_DEPTH) {
            link *l = &r->links[r->link_depth];
            l->start = r->text->len;
            /* An autolink's label is its address already, so it shows no
             * destination of its own. */
            bool autolink = type == MD_SPAN_A && ((MD_SPAN_A_DETAIL *)detail)->is_autolink;
            l->target = target->size && !autolink ? g_strndup(target->text, target->size) : NULL;
            if (target->size && !autolink && !l->target)
                r->failed = true;
        }
        ++r->link_depth;
        break;
    }
    default:
        break;
    }
    return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    (void)detail;
    renderer *r = userdata;
    switch (type) {
    case MD_SPAN_STRONG:
        --r->strong;
        break;
    case MD_SPAN_EM:
        --r->em;
        break;
    case MD_SPAN_DEL:
        --r->del;
        break;
    case MD_SPAN_U:
        --r->underscores;
        break;
#ifdef MD_FLAG_SUBSCRIPTS
    case MD_SPAN_SUBSCRIPT:
        /* Inside a bare link the closing '~' is written with its address. */
        if (!r->bare_end)
            emit(r, "~", 1, current_style(r));
        break;
#endif
    case MD_SPAN_CODE:
        --r->code;
        break;
    case MD_SPAN_A:
    case MD_SPAN_IMG:
        if (r->link_depth > 0 && --r->link_depth < MARKDOWN_DEPTH) {
            link *l = &r->links[r->link_depth];
            /* The destination is shown whenever the label differs from it,
             * so a label never hides where the link leads. A link that
             * closes inside a bare link is part of that address, which
             * shows its markers and its destination as they were typed. */
            if (l->target && !r->bare_end &&
                (r->text->len - l->start != strlen(l->target) ||
                 memcmp(r->text->str + l->start, l->target, strlen(l->target)))) {
                emit(r, " (", 2, MARKDOWN_DECOR);
                emit(r, l->target, strlen(l->target), MARKDOWN_DECOR);
                emit(r, ")", 1, MARKDOWN_DECOR);
            }
            g_free(l->target);
            l->target = NULL;
        }
        break;
    default:
        break;
    }
    return 0;
}

/* Text of one style, split at the line ends that code blocks carry. */
static void plain(renderer *r, const char *at, const char *end, unsigned char style)
{
    while (at < end) {
        const char *line = memchr(at, '\n', (size_t)(end - at));
        const char *stop = line ? line : end;
        if (stop > at) {
            emit(r, at, (size_t)(stop - at), style);
            r->since = 0;
        }
        if (!line)
            break;
        /* A code line's end belongs to the block, so an empty line of code
         * still shows the block's background. */
        newline(r, r->code_block ? MARKDOWN_CODE_BLOCK : 0);
        at = line + 1;
    }
}

static int text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    renderer *r = userdata;
    bool breaks = type == MD_TEXT_BR || type == MD_TEXT_SOFTBR || type == MD_TEXT_NULLCHAR;
    bool sourced = !breaks && size && text >= r->source && text + size <= r->source + r->size;
    size_t offset = sourced ? (size_t)(text - r->source) : 0;
    /* Text from inside the bare link being written continues it. */
    bool continues = sourced && offset >= r->bare_at && offset < r->bare_end;
    if (!continues)
        finish_bare(r);
    switch (type) {
    case MD_TEXT_BR:
    case MD_TEXT_SOFTBR:
        /* Chat messages keep every line break the author typed. */
        newline(r, 0);
        return 0;
    case MD_TEXT_NULLCHAR:
        return 0;
    default:
        break;
    }
    separate(r, text);
    unsigned char style = current_style(r);
    /* The '~' that opened a span inside a bare link is part of its address. */
    if (continues)
        r->tildes = 0;
    for (; r->tildes; --r->tildes)
        emit(r, "~", 1, style);
    /* Entities stay as written: decoding them is not needed for chat text
     * and would add scalars that the sanitized source does not have. */
    const char *at = text, *end = text + size;
    /* Bare links are found here rather than by md4c, whose permissive
     * autolinks drop the whole link when its query or fragment holds
     * characters such as ':', '%' or ','. Code and the label of a link the
     * author wrote out are not searched. */
    bool linkable = sourced && (type == MD_TEXT_NORMAL || type == MD_TEXT_ENTITY) && !r->code &&
                    !r->code_block && !r->link_depth;
    while (at < end && (continues || linkable)) {
        size_t from = (size_t)(at - r->source);
        if (r->bare_end) {
            /* Markers that md4c consumed since the last text of the link. */
            if (from > r->bare_at)
                emit(r, r->source + r->bare_at, from - r->bare_at, r->bare_style);
            size_t stop = MIN((size_t)(end - r->source), r->bare_end);
            emit(r, at, stop - from, r->bare_style);
            r->since = 0;
            r->bare_at = stop;
            if (r->bare_at == r->bare_end)
                r->bare_at = r->bare_end = 0;
            at = r->source + stop;
            continues = false;
            continue;
        }
        if (!linkable)
            break;
        const char *next = end;
        size_t length = 0;
        for (const char *p = at; p < end && !length; ++p)
            if (strchr("hHwW", *p) &&
                (length = markdown_link_length(r->source, r->size, (size_t)(p - r->source))))
                next = p;
        plain(r, at, next, style);
        at = next;
        if (length) {
            r->bare_at = (size_t)(at - r->source);
            r->bare_end = r->bare_at + length;
            r->bare_style = style | MARKDOWN_LINK;
        }
    }
    plain(r, at, end, style);
    consumed(r, text, size);
    return r->failed ? 1 : 0;
}

bool markdown_render(const char *source, markdown_text *out)
{
    memset(out, 0, sizeof(*out));
    if (!source)
        return false;
    renderer r = {.source = source, .size = strlen(source), .line_start = true};
    if (r.size > MARKDOWN_OUTPUT_LIMIT || r.size > (size_t)(MD_SIZE)-1)
        return false;
    r.text = g_string_sized_new(r.size + 1);
    r.styles = g_byte_array_sized_new((guint)r.size + 1);
    MD_PARSER parser = {
        .abi_version = 0,
        .flags = MD_FLAG_NOHTML | MD_FLAG_STRIKETHROUGH | MD_FLAG_UNDERLINE |
                 MD_FLAG_NOINDENTEDCODEBLOCKS
#ifdef MD_FLAG_SUBSCRIPTS
                 /* Parsed only to keep single '~' spans literal. */
                 | MD_FLAG_SUBSCRIPTS
#endif
        ,
        .enter_block = enter_block,
        .leave_block = leave_block,
        .enter_span = enter_span,
        .leave_span = leave_span,
        .text = text,
    };
    int status = md_parse(source, (MD_SIZE)r.size, &parser, &r);
    finish_bare(&r);
    for (int i = 0; i < r.link_depth && i < MARKDOWN_DEPTH; ++i)
        g_free(r.links[i].target);
    if (status || r.failed) {
        g_string_free(r.text, TRUE);
        g_byte_array_free(r.styles, TRUE);
        return false;
    }
    /* A closing block leaves its line end behind; the message ends there. */
    while (r.text->len && r.text->str[r.text->len - 1] == '\n') {
        g_string_truncate(r.text, r.text->len - 1);
        g_byte_array_set_size(r.styles, r.styles->len - 1);
    }
    out->length = r.styles->len;
    out->text = g_string_free(r.text, FALSE);
    out->styles = g_byte_array_free(r.styles, FALSE);
    return true;
}

void markdown_text_free(markdown_text *text)
{
    if (!text)
        return;
    g_free(text->text);
    g_free(text->styles);
    memset(text, 0, sizeof(*text));
}

char *markdown_only_link(const char *source)
{
    if (!source || !g_utf8_validate(source, -1, NULL))
        return NULL;
    const char *start = source, *end = source + strlen(source);
    while (start < end && g_unichar_isspace(g_utf8_get_char(start)))
        start = g_utf8_next_char(start);
    while (end > start && g_unichar_isspace(g_utf8_get_char(g_utf8_prev_char(end))))
        end = g_utf8_prev_char(end);
    if (end - start > 2 && *start == '<' && end[-1] == '>') {
        ++start;
        --end;
    }
    size_t size = (size_t)(end - start);
    if (!size || markdown_link_length(start, size, 0) != size)
        return NULL;
    char *link = g_strndup(start, size);
    if (!g_ascii_strncasecmp(link, "www.", 4)) {
        char *address = g_strconcat("https://", link, NULL);
        g_free(link);
        link = address;
    }
    /* GLib checks the address itself, such as its host and port. */
    if (!g_uri_is_valid(link, G_URI_FLAGS_PARSE_RELAXED, NULL))
        g_clear_pointer(&link, g_free);
    return link;
}
