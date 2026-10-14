#define _POSIX_C_SOURCE 200809L
#include "ui/image_decode.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_PIXELS (16U * 1024U * 1024U)
typedef struct { bool (*cancelled)(void *context); void *context; } decode_guard;
static int decode_interrupt(void *context)
{
    decode_guard *guard = context;
    return guard->cancelled && guard->cancelled(guard->context);
}
void animation_ref(animation *a) { if (a) atomic_fetch_add(&a->references, 1); }
void animation_free(animation *a)
{
    if (!a || atomic_fetch_sub(&a->references, 1) != 1) return;
    for (size_t i = 0; i < a->count; ++i) free(a->pixels[i]);
    free(a);
}
size_t animation_frame_at(const animation *a, uint64_t epoch, uint64_t now)
{
    if (!a || !a->duration) return 0;
    uint64_t at = now >= epoch ? (now - epoch) % a->duration : 0;
    for (size_t i = 0; i < a->count; ++i) {
        if (at < a->delay[i]) return i;
        at -= a->delay[i];
    }
    return 0;
}
static bool encode_pixels(const image_decode_request *j, const unsigned char *pixels, int w, int h,
                          int source_w, int source_h, graphics_image *out)
{
    int top = j->terminal.protocol == GRAPHICS_KITTY ? 0 : j->top;
    int bottom = j->terminal.protocol == GRAPHICS_KITTY ? 0 : j->bottom;
    if (top < 0 || bottom < 0 || top >= h || bottom >= h || top >= h - bottom) return false;
    bool ok = !top && !bottom && j->preview ?
        graphics_image_encode_preview(pixels, w, h, j->terminal, j->image_id, out) :
        graphics_image_encode_id(pixels + (size_t)top * (size_t)w * 3, w, h - top - bottom,
                                 j->terminal, j->image_id, out);
    if (ok) { out->source_width = source_w; out->source_height = source_h;
              out->crop_top = top; out->crop_bottom = bottom; }
    return ok;
}
bool image_decode_file(const image_decode_request *j, graphics_image *out, animation **animated, bool (*cancelled)(void *context), void *context)
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
    frames = calloc(1, sizeof(*frames));
    if (frames) atomic_init(&frames->references, 1);
    if (!frame || !packet || !frames) goto done;
    bool gif = parameters->codec_id == AV_CODEC_ID_GIF;
    int read_status = 0;
    for (unsigned n = 0; n < 4096 && (read_status = av_read_frame(format, packet)) >= 0; ++n) {
        if (decode_interrupt(&guard)) goto done;
        if (packet->stream_index != stream) { av_packet_unref(packet); continue; }
        int64_t duration = av_rescale_q(packet->duration, format->streams[stream]->time_base, (AVRational){1, 1000});
        if (avcodec_send_packet(decoder, packet) < 0) goto done;
        av_packet_unref(packet);
        if (avcodec_receive_frame(decoder, frame) < 0) goto done;
        if (frame->width < 1 || frame->height < 1 ||
            (size_t)frame->width > IMAGE_PIXELS / (size_t)frame->height) goto done;
        if (!frames->count) {
            int w = j->width, h = j->height;
            if (w < 1 || h < 1 || w > 640 || h > 360) goto done;
            double ratio = fmin((double)w / frame->width, (double)h / frame->height);
            if (j->native && ratio > 1) ratio = 1;
            int sw = (int)(frame->width * ratio), sh = (int)(frame->height * ratio);
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
            if (j->native) { w = sw; h = sh; }
            frames->width = w; frames->height = h;
            frames->source_width = frame->width; frames->source_height = frame->height;
            rgba = malloc((size_t)sw * (size_t)sh * 4);
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
        size_t bytes = (size_t)frames->width * (size_t)frames->height * 3;
        if (bytes > ANIMATION_BYTES - frames->bytes) goto done;
        unsigned char *rgb = malloc(bytes);
        if (!rgb) goto done;
        unsigned char *dest[] = {rgba, NULL, NULL, NULL}; int stride[] = {sw * 4, 0, 0, 0};
        if (sws_scale(scale, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height, dest, stride) != sh) { free(rgb); goto done; }
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
        if (!gif) break;
    }
    if (!frames->count || (gif && read_status != AVERROR_EOF) || decode_interrupt(&guard)) goto done;
    image_decode_request encoding = *j; encoding.animation = frames;
    ok = encode_pixels(&encoding, frames->pixels[0], frames->width, frames->height,
                       frames->source_width, frames->source_height, out);
    if (ok && frames->count > 1) { *animated = frames; frames = NULL; }
 done:
    animation_free(frames); av_dict_free(&options); sws_freeContext(scale); free(rgba);
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&decoder);
    avformat_close_input(&format); return ok;
}
bool image_encode_frame(const image_decode_request *request, graphics_image *out)
{
    animation *a = request->animation;
    if (!a || request->frame >= a->count) return false;
    return encode_pixels(request, a->pixels[request->frame], a->width, a->height,
                         a->source_width, a->source_height, out);
}
