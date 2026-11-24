#ifndef PAPO_MARKDOWN_H
#define PAPO_MARKDOWN_H

#include <stdbool.h>
#include <stddef.h>

/* Per-scalar style bits. MENTION is never produced here: it is the bit the
 * history already uses for resolved mentions, so both masks can be merged. */
enum {
    MARKDOWN_MENTION = 1,
    MARKDOWN_BOLD = 2,
    MARKDOWN_ITALIC = 4,
    MARKDOWN_STRIKE = 8,
    MARKDOWN_CODE = 16,
    MARKDOWN_LINK = 32,
    MARKDOWN_DECOR = 64, /* Quote bars, list bullets, rules and link targets. */
    MARKDOWN_UNDERLINE = 128,
    /* Text of a fenced code block, including the end of each of its lines:
     * the history draws those lines as one box. It is the only style with
     * both of these bits; test it with markdown_code_block. */
    MARKDOWN_CODE_BLOCK = MARKDOWN_CODE | MARKDOWN_DECOR
};

static inline bool markdown_code_block(unsigned char style)
{
    return (style & MARKDOWN_CODE_BLOCK) == MARKDOWN_CODE_BLOCK;
}

/* Owned result: text is NUL-terminated UTF-8 without Markdown markers, and
 * styles holds one byte for each Unicode scalar of text. */
typedef struct {
    char *text;
    unsigned char *styles;
    size_t length; /* Scalars in text, and entries in styles. */
} markdown_text;

/* Renders chat Markdown as styled plain text for the history: CommonMark
 * with the conventions of chat clients, where '__' underlines, only '~~'
 * strikes through, bare URLs are links and raw HTML stays literal. source
 * must be valid UTF-8 already sanitized for the terminal: every scalar of the
 * output is either a scalar of source or a printable decoration, so its
 * positions match a decoded buffer. Single newlines and blank lines written
 * by the author are kept. False on failure, with out cleared; callers then
 * show source as is. */
bool markdown_render(const char *source, markdown_text *out);
void markdown_text_free(markdown_text *text);

/* Length in bytes of the bare link that begins at text[at], or 0 when none
 * does: an address written from "http://", "https://" or "www." up to the
 * next blank, as chat clients and GitHub recognize it. Punctuation that ends
 * the sentence and a bracket the address did not open stay out of it. text
 * is valid UTF-8 of size bytes; nothing past them is read. */
size_t markdown_link_length(const char *text, size_t size, size_t at);

/* The address to open when source is a single bare link and nothing else,
 * alone or between '<' and '>'; NULL otherwise. Owned by the caller. It
 * starts with "http://" or "https://", which a "www." address gains, and is a
 * well-formed address, but it still comes from untrusted text. */
char *markdown_only_link(const char *source);

#endif
