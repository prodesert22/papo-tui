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
} renderer;

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
            /* An autolink's label is its address already (www. links only
             * gain a scheme), so it shows no destination of its own. */
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
             * so a label never hides where the link leads. */
            if (l->target && (r->text->len - l->start != strlen(l->target) ||
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

static int text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    renderer *r = userdata;
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
    for (; r->tildes; --r->tildes)
        emit(r, "~", 1, style);
    /* Entities stay as written: decoding them is not needed for chat text
     * and would add scalars that the sanitized source does not have. */
    const char *at = text, *end = text + size;
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
                 MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_PERMISSIVEWWWAUTOLINKS |
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
