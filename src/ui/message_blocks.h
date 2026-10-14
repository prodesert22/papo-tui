#ifndef PAPO_MESSAGE_BLOCKS_H
#define PAPO_MESSAGE_BLOCKS_H
#include "net/auth.h"
#include "model/mentions.h"
#include <ncurses.h>
#define MESSAGE_MEDIA_LABEL 512
#define MESSAGE_COLUMNS 3
/* Owned presentation, borrowed input JSON only during construction. Stable
 * block keys and Unicode offsets survive reflow and inserts in other blocks. */
typedef struct {
    char id[37], message[37], label[MESSAGE_MEDIA_LABEL], mime[32], role[12];
    auth_media_kind kind;
    bool video;
    uint64_t version;
} message_media;
typedef struct {
    char key[128];
    char *text;
    size_t offset;
    int x, width;
    bool bold;
    unsigned char *highlight;
    size_t highlight_length;
} message_segment;
typedef struct {
    message_segment segments[MESSAGE_COLUMNS];
    int count;
    bool card, top, bottom;
    unsigned color;
    int media_index, media_row, media_height, media_x, media_width;
} message_row;
typedef struct {
    message_row *rows;
    size_t count, capacity;
    message_media *media;
    size_t media_count;
    mention_resolver resolver;
    void *resolver_context;
    bool self_mention;
} message_blocks;
uint64_t message_media_version(const json_t *entry);
bool message_blocks_build(message_blocks *out, json_t *message, const char *author,
                          const char *reply, int width, bool graphics);
bool message_blocks_build_mentions(message_blocks *out, json_t *message, const char *author,
                          const char *reply, int width, bool graphics,
                          mention_resolver resolver, void *context, const char *self);
void message_blocks_free(message_blocks *out);
void message_colors_begin(void);
void message_row_draw(WINDOW *window, const message_row *row, int y, int x,
                      int width, int pair, int accent);
#endif
