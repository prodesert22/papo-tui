#ifndef PAPO_MESSAGE_BLOCKS_H
#define PAPO_MESSAGE_BLOCKS_H
#include "model/assets/catalog.h"
#include "model/text/mentions.h"
#include "net/auth/client.h"
#include "ui/input/text.h"
#include <ncurses.h>
#define MESSAGE_MEDIA_LABEL 512
#define MESSAGE_COLUMNS 3
#define MESSAGE_AVATAR_PIXELS 48
/* Owned presentation, borrowed input JSON only during construction. Stable
 * block keys and Unicode offsets survive reflow and inserts in other blocks. */
typedef struct {
    char id[37], message[37], label[MESSAGE_MEDIA_LABEL], mime[32], role[12];
    auth_media_kind kind;
    bool video, size_known, full_width;
    int64_t size_bytes;
    int source_width, source_height;
    char original_file_name[MESSAGE_MEDIA_LABEL];
    char moderation[32], created_at[64], thumbnail_id[37];
    uint64_t version;
    unsigned position; /* Position within the message, independent of resource identity. */
} message_media;
typedef struct {
    char key[128];
    char *text;
    size_t offset;
    int x, width;
    bool bold, muted, link;
    unsigned char *highlight;
    size_t highlight_length;
} message_segment;
typedef struct {
    int index, row, height, x, width;
} message_media_region;
typedef struct {
    uint32_t id;
    int x;
    size_t offset;
    char key[128];
} message_icon_region;
typedef struct {
    int x, y;                  /* Position in the message; neighboring tiles can share y. */
    int tile_width, tile_rows; /* Nonzero only on the first row of a tile. */
    message_segment segments[MESSAGE_COLUMNS];
    int count;
    bool card, top, bottom;
    int card_width;
    unsigned color;
    message_media_region media[2];
    int media_count;
    message_icon_region *icons;
    size_t icon_count;
    text_icon_painter icon_painter;
    void *icon_context;
    bool header, reaction;
    int indent;
} message_row;
typedef void (*message_media_measure)(void *context, const message_media *item, int available_width,
                                      int *width, int *height);
typedef struct {
    int row, x, width;
    size_t index;
    bool mine;
} message_reaction_region;
typedef struct {
    message_reaction_region reactions[20];
    size_t reaction_count;
    message_row *rows;
    size_t count, capacity;
    int height; /* Physical rows after wrapping image tiles. */
    message_media *media;
    size_t media_count;
    message_media_measure measure;
    void *measure_context;
    mention_resolver resolver;
    void *resolver_context;
    bool self_mention;
    text_icon_resolver icon_resolver;
    text_icon_painter icon_painter;
    void *icon_context;
} message_blocks;
void message_reaction_label(chat_assets *assets, const json_t *emoji, char *label, size_t capacity);
uint64_t message_media_version(const json_t *entry);
/* Adjacent messages can share a header only within the same channel and
 * author identity. Webhook display overrides remain distinct. */
bool message_same_author(const json_t *previous, const json_t *message);
bool message_blocks_build(message_blocks *out, json_t *message, const char *author,
                          const char *reply, int width, bool graphics);
bool message_blocks_build_sized(message_blocks *out, json_t *message, const char *author,
                                const char *reply, int width, bool graphics,
                                message_media_measure measure, void *context);
bool message_blocks_build_mentions(message_blocks *out, json_t *message, const char *author,
                                   const char *reply, int width, bool graphics,
                                   mention_resolver resolver, void *context, const char *self);
bool message_blocks_build_sized_mentions(message_blocks *out, json_t *message, const char *author,
                                         const char *reply, int width, bool graphics,
                                         message_media_measure measure, void *measure_context,
                                         mention_resolver resolver, void *resolver_context,
                                         const char *self);
void message_blocks_free(message_blocks *out);
bool message_blocks_build_visual(message_blocks *out, json_t *message, const char *author,
                                 const char *reply, int width, bool graphics,
                                 message_media_measure measure, void *measure_context,
                                 mention_resolver resolver, void *resolver_context,
                                 const char *self, text_icon_resolver icons,
                                 text_icon_painter painter, void *icon_context, bool continuation);
void message_colors_begin(void);
void message_row_draw(WINDOW *window, const message_row *row, int y, int x, int width, int pair,
                      int accent);
#endif
