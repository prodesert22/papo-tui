#ifndef PAPO_MEDIA_WIRE_H
#define PAPO_MEDIA_WIRE_H
#include "ui/media/media_backend.h"
#include <glib.h>
#define MEDIA_WIRE_VERSION 2U
#define MEDIA_WIRE_LIMIT (160U * 1024U * 1024U)
enum {
    MEDIA_RPC_DECODE,
    MEDIA_RPC_FRAME,
    MEDIA_RPC_ENCODE,
    MEDIA_RPC_GLYPH,
    MEDIA_RPC_VIDEO_OPEN,
    MEDIA_RPC_VIDEO_CLOSE,
    MEDIA_RPC_VIDEO_UPDATE,
    MEDIA_RPC_VIDEO_CHANGED,
    MEDIA_RPC_VIDEO_RENDER,
    MEDIA_RPC_DETECT,
    MEDIA_RPC_CANCEL
};
/* All serialized structures contain scalars only. Buffers are copied and owned
 * by the receiver; no allocator, pointer, auth object or descriptor crosses IPC. */
/* Owned serialized buffer; large messages bypass malloc arenas. */
GBytes *media_wire_store(GVariant *value);
/* Owned private buffer. Fill it before publishing a variant or immutable bytes. */
GBytes *media_wire_buffer(size_t size, void **data);
GVariant *media_wire_bytes(const void *data, size_t length);
bool media_wire_copy(GVariant *value, void *out, size_t length);
GVariant *media_wire_request(const image_decode_request *request);
bool media_wire_read_request(GVariant *value, image_decode_request *out, char **path);
GVariant *media_wire_image(const graphics_image *image);
bool media_wire_read_image(GVariant *value, graphics_image *image);
GVariant *media_wire_animation(const animation *frames);
bool media_wire_read_animation(GVariant *value, animation **frames);
#endif
