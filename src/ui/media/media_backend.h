#ifndef PAPO_MEDIA_BACKEND_H
#define PAPO_MEDIA_BACKEND_H
#include "ui/media/image_decode.h"

#define PAPO_MEDIA_BACKEND_ABI 8U
/* Bound larger modal surfaces while keeping their buffers within the cache budget. */
#define VIDEO_FRAME_PIXELS (7U * 1024U * 1024U)
#define VIDEO_KITTY_FRAME_PIXELS (960U * 540U)
/* Private, versioned module boundary. No curses, auth or mutable models cross it.
 * Pixel buffers use the process allocator. Modules remain resident after use;
 * every player/context and frame must still be released by its owner. */
typedef struct {
    unsigned abi;
    size_t size, request_size, animation_size, graphics_size;
    bool (*encode)(const unsigned char *, int, int, graphics_terminal, uint32_t, graphics_image *);
    bool (*preview)(const unsigned char *, int, int, graphics_terminal, uint32_t, graphics_image *);
    bool (*region)(const unsigned char *, int, int, int, int, int, int, graphics_terminal, uint32_t,
                   graphics_image *);
    void (*release_frames)(animation *);
    void *(*allocate)(size_t);
    void *(*allocate_zero)(size_t, size_t);
    void (*release)(void *);
} image_codec_host;
typedef struct {
    unsigned abi;
    size_t size;
    bool (*decode)(const image_decode_request *, graphics_image *, animation **, bool (*)(void *),
                   void *);
    bool (*encode_frame)(const image_decode_request *, graphics_image *);
} image_codec_api;
typedef const image_codec_api *(*image_codec_entry)(const image_codec_host *);

typedef struct {
    int pause, mute, seek, volume;
    bool seek_absolute, volume_absolute;
    double position, volume_level;
} video_controls;
typedef struct {
    bool paused, muted, audio_missing, invalid;
    double position, duration, volume;
    int width, height; /* Display dimensions reported by the player, or zero until known. */
} video_info;
typedef struct {
    unsigned abi;
    size_t size, controls_size, info_size;
    void *(*open)(const char *);
    void (*close)(void *);
    void (*update)(void *, const video_controls *, video_info *);
    bool (*changed)(void *);
    bool (*render)(void *, unsigned char *, int, int);
    /* Callbacks only wake the worker; they must not call back into libmpv. */
    void (*set_wakeup)(void *, void (*)(void *), void *);
} video_player_api;
typedef const video_player_api *(*video_player_entry)(unsigned, size_t);

/* Thread-safe, first call loads on the calling worker. Failure is cached until
 * restart. Successful tables are immutable and valid for the process lifetime. */
const image_codec_api *media_image_backend(void);
const video_player_api *media_video_backend(void);
#endif
