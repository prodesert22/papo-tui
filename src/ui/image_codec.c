#define _POSIX_C_SOURCE 200809L
#include "ui/media_backend.h"
#include <gmodule.h>
#include <libavutil/log.h>
static image_codec_host host;
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_PIXELS GRAPHICS_IMAGE_PIXELS
_Static_assert(IMAGE_PIXEL_PADDING >= AV_INPUT_BUFFER_PADDING_SIZE, "insufficient SIMD pixel padding");
typedef struct { bool (*cancelled)(void *context); void *context; } decode_guard;
static int decode_interrupt(void *context)
{
    decode_guard *guard = context;
    return guard->cancelled && guard->cancelled(guard->context);
}
static bool encode_pixels(const image_decode_request *j, const unsigned char *pixels, int w, int h,
                          int source_w, int source_h, graphics_image *out)
{
    int top = j->terminal.protocol == GRAPHICS_KITTY ? 0 : j->top;
    int bottom = j->terminal.protocol == GRAPHICS_KITTY ? 0 : j->bottom;
    if (top < 0 || bottom < 0 || top >= h || bottom >= h || top >= h - bottom) return false;
    bool ok = !top && !bottom && j->preview ?
        host.preview(pixels, w, h, j->terminal, j->image_id, out) :
        host.encode(pixels + (size_t)top * (size_t)w * 3, w, h - top - bottom,
                                 j->terminal, j->image_id, out);
    if (ok) { out->source_width = source_w; out->source_height = source_h;
              out->crop_top = top; out->crop_bottom = bottom; }
    return ok;
}
static bool decode_file(const image_decode_request *j, graphics_image *out, animation **animated, bool (*cancelled)(void *context), void *context)
{
    memset(out, 0, sizeof(*out)); *animated = NULL;
    if (cancelled && cancelled(context)) return false;
    AVFormatContext *format = avformat_alloc_context();
    AVCodecContext *decoder = NULL;
    AVFrame *frame = NULL;
    AVPacket *packet = NULL;
    struct SwsContext *scale = NULL;
    unsigned char *rgba = NULL;
    AVDictionary *options = NULL;
    animation *frames = NULL;
    bool ok = false;
    if (!format) goto done;
    decode_guard guard = {cancelled, context};
    format->interrupt_callback = (AVIOInterruptCB){decode_interrupt, &guard};
    format->max_streams = 1;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    av_dict_set(&options, "ignore_loop", "1", 0);
    if (avformat_open_input(&format, j->path, NULL, &options) < 0) goto done;
    AVDictionary *decode_options = NULL;
    av_dict_set(&decode_options, "max_pixels", "16777216", 0);
    av_dict_set(&decode_options, "threads", "1", 0);
    int info = avformat_find_stream_info(format, &decode_options);
    av_dict_free(&decode_options);
    if (info < 0) goto done;
    int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (stream < 0) goto done;
    AVCodecParameters *parameters = format->streams[stream]->codecpar;
    if (parameters->codec_id != AV_CODEC_ID_MJPEG && parameters->codec_id != AV_CODEC_ID_PNG &&
        parameters->codec_id != AV_CODEC_ID_WEBP && parameters->codec_id != AV_CODEC_ID_GIF) goto done;
    if (parameters->width < 1 || parameters->height < 1 ||
        (size_t)parameters->width > IMAGE_PIXELS / (size_t)parameters->height) goto done;
    const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec || !(decoder = avcodec_alloc_context3(codec))) goto done;
    decoder->max_pixels = IMAGE_PIXELS;
    decoder->thread_count = 1;
    if (avcodec_parameters_to_context(decoder, parameters) < 0 || avcodec_open2(decoder, codec, NULL) < 0) goto done;
    frame = av_frame_alloc(); packet = av_packet_alloc();
    frames = host.allocate_zero(1, sizeof(*frames));
    if (frames) atomic_init(&frames->references, 1);
    if (!frame || !packet || !frames) goto done;
    bool gif = parameters->codec_id == AV_CODEC_ID_GIF, static_only = j->static_only;
    int read_status = 0;
    for (unsigned n = 0; n < 4096 && (read_status = av_read_frame(format, packet)) >= 0; ++n) {
        if (decode_interrupt(&guard)) goto done;
        if (packet->stream_index != stream) { av_packet_unref(packet); continue; }
        int64_t duration = av_rescale_q(packet->duration, format->streams[stream]->time_base, (AVRational){1, 1000});
        if (avcodec_send_packet(decoder, packet) < 0) goto done;
        av_packet_unref(packet);
        int decoded = avcodec_receive_frame(decoder, frame);
        /* GIF extensions can form packets without producing an image. */
        if (decoded == AVERROR(EAGAIN)) continue;
        if (decoded < 0) goto done;
        if (frame->width < 1 || frame->height < 1 ||
            (size_t)frame->width > IMAGE_PIXELS / (size_t)frame->height) goto done;
        if (!frames->count) {
            int w = j->width, h = j->height;
            if (!graphics_image_capacity(w, h)) goto done;
            double ratio = fmin((double)w / frame->width, (double)h / frame->height);
            if (j->native && ratio > 1) ratio = 1;
            int sw = (int)(frame->width * ratio), sh = (int)(frame->height * ratio);
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
            if (j->native) { w = sw; h = sh; }
            frames->width = w; frames->height = h;
            frames->source_width = frame->width; frames->source_height = frame->height;
            size_t rgba_bytes = (size_t)sw * (size_t)sh * 4;
            rgba = host.allocate_zero(1, rgba_bytes + IMAGE_PIXEL_PADDING);
            scale = sws_getContext(frame->width, frame->height, frame->format, sw, sh,
                                  AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
            if (!rgba || !scale) goto done;
        }
        if (frame->width != frames->source_width || frame->height != frames->source_height ||
            frames->count == ANIMATION_FRAMES) goto done;
        int sw = 0, sh = 0;
        double ratio = fmin((double)j->width / frame->width, (double)j->height / frame->height);
        if (j->native && ratio > 1) ratio = 1;
        sw = (int)(frame->width * ratio); sh = (int)(frame->height * ratio);
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        size_t bytes = image_frame_bytes(frames->width, frames->height);
        if (bytes > ANIMATION_BYTES || frames->bytes > ANIMATION_BYTES - bytes) {
            /* Keep the first frame when a larger viewport would exhaust the
             * animation budget; a single raster is bounded by IMAGE_PIXELS. */
            static_only = true;
            if (frames->count) {
                for (size_t i = 1; i < frames->count; ++i) { host.release(frames->pixels[i]); frames->pixels[i] = NULL; }
                frames->count = 1; frames->bytes = bytes; frames->duration = frames->delay[0];
                break;
            }
        }
        unsigned char *rgb = host.allocate(bytes);
        if (!rgb) goto done;
        memset(rgb + bytes - IMAGE_PIXEL_PADDING, 0, IMAGE_PIXEL_PADDING);
        unsigned char *dest[] = {rgba, NULL, NULL, NULL}; int stride[] = {sw * 4, 0, 0, 0};
        if (sws_scale(scale, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height, dest, stride) != sh) { host.release(rgb); goto done; }
        for (int y = 0; y < frames->height; ++y) for (int x = 0; x < frames->width; ++x) {
            size_t at = ((size_t)y * (size_t)frames->width + (size_t)x) * 3;
            const unsigned char bg[] = {0x15, 0x18, 0x1e};
            for (int c = 0; c < 3; ++c) {
                unsigned alpha = 0, color = 0;
                if (y < sh && x < sw) { size_t pos = ((size_t)y * (size_t)sw + (size_t)x) * 4; alpha = rgba[pos + 3]; color = rgba[pos + (size_t)c]; }
                rgb[at + (size_t)c] = (unsigned char)((color * alpha + bg[c] * (255 - alpha)) / 255);
            }
        }
        frames->pixels[frames->count] = rgb;
        uint32_t delay = duration > 0 && duration <= 60000 ? (uint32_t)duration : 100;
        if (delay < 10) delay = 10;
        frames->delay[frames->count++] = delay; frames->duration += delay; frames->bytes += bytes;
        if (!gif || static_only) break;
    }
    if (!frames->count || (gif && !static_only && read_status != AVERROR_EOF) || decode_interrupt(&guard)) goto done;
    image_decode_request encoding = *j; encoding.animation = frames;
    ok = j->decode_only || encode_pixels(&encoding, frames->pixels[0], frames->width, frames->height,
                       frames->source_width, frames->source_height, out);
    if (ok && (j->retain_static || frames->count > 1)) { *animated = frames; frames = NULL; }
 done:
    host.release_frames(frames); av_dict_free(&options); sws_freeContext(scale); host.release(rgba);
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&decoder);
    avformat_close_input(&format); return ok;
}
static bool encode_frame(const image_decode_request *request, graphics_image *out)
{
    animation *a = request->animation;
    if (!a || request->frame >= a->count) return false;
    int w = request->width, h = request->height;
    if (!graphics_image_capacity(w, h)) return false;
    double ratio = fmin((double)w / a->source_width, (double)h / a->source_height);
    if (request->native && ratio > 1) ratio = 1;
    int sw = (int)(a->source_width * ratio), sh = (int)(a->source_height * ratio);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (request->native) { w = sw; h = sh; }
    if (w == a->width && h == a->height)
        return encode_pixels(request, a->pixels[request->frame], w, h, a->source_width, a->source_height, out);
    size_t bytes = image_frame_bytes(w, h);
    unsigned char *pixels = host.allocate(bytes);
    if (!pixels) return false;
    memset(pixels + bytes - IMAGE_PIXEL_PADDING, 0, IMAGE_PIXEL_PADDING);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        unsigned char *at = pixels + ((size_t)y * (size_t)w + (size_t)x) * 3;
        at[0] = 0x15; at[1] = 0x18; at[2] = 0x1e;
    }
    struct SwsContext *scale = sws_getContext(a->width, a->height, AV_PIX_FMT_RGB24,
        sw, sh, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
    const uint8_t *src[] = {a->pixels[request->frame], NULL, NULL, NULL};
    uint8_t *dst[] = {pixels, NULL, NULL, NULL};
    int strides[] = {a->width * 3, 0, 0, 0}, dest_strides[] = {w * 3, 0, 0, 0};
    bool ok = scale && sws_scale(scale, src, strides, 0, a->height, dst, dest_strides) == sh;
    if (ok) ok = encode_pixels(request, pixels, w, h, a->source_width, a->source_height, out);
    sws_freeContext(scale); host.release(pixels); return ok;
}

G_MODULE_EXPORT const image_codec_api *papo_image_backend(const image_codec_host *callbacks)
{
    if (!callbacks || callbacks->abi != PAPO_MEDIA_BACKEND_ABI || callbacks->size != sizeof(*callbacks) ||
        callbacks->request_size != sizeof(image_decode_request) || callbacks->animation_size != sizeof(animation) ||
        callbacks->graphics_size != sizeof(graphics_image) || !callbacks->encode || !callbacks->preview || !callbacks->release_frames ||
        !callbacks->allocate || !callbacks->allocate_zero || !callbacks->release)
        return NULL;
    host = *callbacks;
    av_log_set_level(AV_LOG_QUIET);
    static const image_codec_api api = {PAPO_MEDIA_BACKEND_ABI, sizeof(image_codec_api), decode_file, encode_frame};
    return &api;
}
