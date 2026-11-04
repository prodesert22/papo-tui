#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/media_backend.h"
#include <gmodule.h>
#include <libavutil/log.h>
static image_codec_host host;
#include <errno.h>
#include <glib/gstdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <librsvg/rsvg.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_PIXELS GRAPHICS_IMAGE_PIXELS
_Static_assert(IMAGE_PIXEL_PADDING >= AV_INPUT_BUFFER_PADDING_SIZE,
               "insufficient SIMD pixel padding");
typedef struct {
    bool (*cancelled)(void *context);
    void *context;
} decode_guard;
static int decode_interrupt(void *context)
{
    decode_guard *guard = context;
    return guard->cancelled && guard->cancelled(guard->context);
}
static bool encode_pixels(const image_decode_request *j, const unsigned char *pixels, int w, int h,
                          int source_w, int source_h, graphics_image *out)
{
    int top = j->terminal.protocol == GRAPHICS_KITTY && !j->region ? 0 : j->top;
    int bottom = j->terminal.protocol == GRAPHICS_KITTY && !j->region ? 0 : j->bottom;
    if (top < 0 || bottom < 0 || top >= h || bottom >= h || top >= h - bottom)
        return false;
    bool ok = j->region ? host.region(pixels, w, h, top, bottom, j->left, j->right, j->terminal,
                                      j->image_id, out)
              : !top && !bottom && j->preview
                  ? host.preview(pixels, w, h, j->terminal, j->image_id, out)
                  : host.encode(pixels + (size_t)top * (size_t)w * 3, w, h - top - bottom,
                                j->terminal, j->image_id, out);
    if (ok) {
        out->source_width = source_w;
        out->source_height = source_h;
        out->crop_top = top;
        out->crop_bottom = bottom;
    }
    return ok;
}
/* Kitty and iTerm2 composite unassociated RGBA over the terminal's own cells,
 * so transparent pixels keep showing whatever is behind them. */
static bool keeps_alpha(const image_decode_request *j)
{
    return j->terminal.protocol == GRAPHICS_KITTY || j->terminal.protocol == GRAPHICS_ITERM2;
}
static bool encode_alpha_pixels(const image_decode_request *j, const unsigned char *rgba, int w,
                                int h, int source_w, int source_h, graphics_image *out)
{
    int top = j->terminal.protocol == GRAPHICS_KITTY && !j->region ? 0 : j->top;
    int bottom = j->terminal.protocol == GRAPHICS_KITTY && !j->region ? 0 : j->bottom;
    if (top < 0 || bottom < 0 || top >= h || bottom >= h || top >= h - bottom)
        return false;
    bool ok =
        host.encode_rgba(rgba, w, h, top, bottom, j->region ? j->left : 0, j->region ? j->right : 0,
                         !j->region && j->preview, j->terminal, j->image_id, out);
    if (ok) {
        out->source_width = source_w;
        out->source_height = source_h;
        out->crop_top = top;
        out->crop_bottom = bottom;
    }
    return ok;
}
/* Places an unassociated sw x sh RGBA picture on the frame canvas: kept as
 * RGBA when it has transparency, otherwise blended into the matte as RGB. */
static bool store_frame(const image_decode_request *j, animation *frames, const unsigned char *rgba,
                        int sw, int sh, bool alpha, size_t bytes, uint32_t delay)
{
    size_t channels = alpha ? 4U : 3U;
    unsigned char *rgb = host.allocate_zero(1, bytes);
    if (!rgb)
        return false;
    for (int y = 0; y < frames->height; ++y)
        for (int x = 0; x < frames->width; ++x) {
            size_t at = ((size_t)y * frames->width + x) * channels;
            int px = x - (j->center ? (frames->width - sw) / 2 : 0);
            int py = y - (j->center ? (frames->height - sh) / 2 : 0);
            bool inside = py >= 0 && px >= 0 && py < sh && px < sw;
            size_t pos = inside ? ((size_t)py * sw + px) * 4 : 0;
            if (alpha) {
                if (inside)
                    memcpy(rgb + at, rgba + pos, 4);
            } else {
                uint32_t background = j->matte ? j->background : 0x000000;
                for (int c = 0; c < 3; ++c) {
                    unsigned opacity = inside ? rgba[pos + 3] : 0;
                    unsigned color = inside ? rgba[pos + (size_t)c] : 0;
                    unsigned bg = (background >> (16 - 8 * c)) & 255;
                    rgb[at + (size_t)c] =
                        (unsigned char)((color * opacity + bg * (255 - opacity)) / 255);
                }
            }
        }
    if (delay < 10)
        delay = 10;
    frames->rgba[frames->count] = alpha;
    frames->pixels[frames->count] = rgb;
    frames->delay[frames->count++] = delay;
    frames->duration += delay;
    frames->bytes += bytes;
    return true;
}
static bool transparent(const unsigned char *rgba, int w, int h)
{
    for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; ++i)
        if (rgba[i * 4 + 3] != 255)
            return true;
    return false;
}

/* SVG text, possibly after a BOM, a declaration, comments or a doctype, or a
 * gzip stream (.svgz: no raster format FFmpeg reads here is gzip). */
#define SVG_SNIFF_BYTES 4096
#define SVG_FILE_LIMIT (32U * 1024U * 1024U)
/* The CSS default size of a replaced element without dimensions. */
#define SVG_DEFAULT_WIDTH 300
#define SVG_DEFAULT_HEIGHT 150
static bool svg_document(const char *path)
{
    FILE *file = g_fopen(path, "rb");
    if (!file)
        return false;
    unsigned char head[SVG_SNIFF_BYTES];
    size_t n = fread(head, 1, sizeof(head), file);
    fclose(file);
    if (n >= 2 && head[0] == 0x1f && head[1] == 0x8b)
        return true;
    size_t at = n >= 3 && head[0] == 0xef && head[1] == 0xbb && head[2] == 0xbf ? 3 : 0;
    while (at < n && g_ascii_isspace(head[at]))
        ++at;
    if (at >= n || head[at] != '<')
        return false;
    for (size_t i = at; i + 4 <= n; ++i)
        if (!memcmp(head + i, "<svg", 4))
            return true;
    return false;
}
/* Intrinsic size in CSS pixels: width/height, else the viewBox, else the
 * CSS default. Huge sizes keep their aspect ratio inside the pixel budget. */
static void svg_size(RsvgHandle *svg, double *width, double *height)
{
    if (!rsvg_handle_get_intrinsic_size_in_pixels(svg, width, height) || !(*width > 0) ||
        !(*height > 0) || !isfinite(*width) || !isfinite(*height)) {
        gboolean has_width, has_height, has_viewbox;
        RsvgLength w, h;
        RsvgRectangle viewbox;
        rsvg_handle_get_intrinsic_dimensions(svg, &has_width, &w, &has_height, &h, &has_viewbox,
                                             &viewbox);
        if (has_viewbox && viewbox.width > 0 && viewbox.height > 0 && isfinite(viewbox.width) &&
            isfinite(viewbox.height)) {
            *width = viewbox.width;
            *height = viewbox.height;
        } else {
            *width = SVG_DEFAULT_WIDTH;
            *height = SVG_DEFAULT_HEIGHT;
        }
    }
    double scale = fmin(1, fmin(16384 / *width, 16384 / *height));
    scale = fmin(scale, sqrt(IMAGE_PIXELS / (*width * *height)));
    *width = fmax(1, round(*width * scale));
    *height = fmax(1, round(*height * scale));
}
/* Renders the document at the requested size: a vector has no native pixel
 * size to cap enlargement, so the player and history stay sharp. The data
 * has no base location, so librsvg refuses every external reference. */
static bool decode_svg(const image_decode_request *j, graphics_image *out, animation **animated,
                       bool (*cancelled)(void *context), void *context)
{
    memset(out, 0, sizeof(*out));
    *animated = NULL;
    GStatBuf status;
    if (g_stat(j->path, &status) || status.st_size <= 0 || status.st_size > SVG_FILE_LIMIT)
        return false;
    gchar *data = NULL;
    gsize length = 0;
    if (!g_file_get_contents(j->path, &data, &length, NULL))
        return false;
    RsvgHandle *svg = rsvg_handle_new_from_data((const guint8 *)data, length, NULL);
    g_free(data);
    if (!svg)
        return false;
    bool ok = false;
    cairo_surface_t *surface = NULL;
    unsigned char *rgba = NULL;
    animation *frames = NULL;
    double source_w, source_h;
    svg_size(svg, &source_w, &source_h);
    int w = j->width, h = j->height;
    if ((cancelled && cancelled(context)) || !graphics_image_capacity(w, h))
        goto done;
    double ratio = j->cover ? fmax(w / source_w, h / source_h) : fmin(w / source_w, h / source_h);
    int sw = (int)fmax(1, source_w * ratio), sh = (int)fmax(1, source_h * ratio);
    if (!graphics_image_capacity(sw, sh))
        goto done;
    if (j->native) {
        w = sw;
        h = sh;
    }
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        goto done;
    cairo_t *cr = cairo_create(surface);
    RsvgRectangle viewport = {0, 0, sw, sh};
    bool rendered = rsvg_handle_render_document(svg, cr, &viewport, NULL);
    cairo_destroy(cr);
    if (!rendered || (cancelled && cancelled(context)))
        goto done;
    cairo_surface_flush(surface);
    /* Cairo stores premultiplied native-endian ARGB; the pipeline expects
     * unassociated RGBA bytes. */
    rgba = host.allocate_zero(1, (size_t)sw * sh * 4 + IMAGE_PIXEL_PADDING);
    if (!rgba)
        goto done;
    const unsigned char *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            uint32_t p = *(const uint32_t *)(pixels + (size_t)y * stride + (size_t)x * 4);
            unsigned a = p >> 24, channel[3] = {(p >> 16) & 255, (p >> 8) & 255, p & 255};
            unsigned char *to = rgba + ((size_t)y * sw + x) * 4;
            for (int c = 0; c < 3; ++c)
                to[c] = a ? (unsigned char)MIN(255U, (channel[c] * 255 + a / 2) / a) : 0;
            to[3] = (unsigned char)a;
        }
    frames = host.allocate_zero(1, sizeof(*frames));
    if (!frames)
        goto done;
    atomic_init(&frames->references, 1);
    frames->width = w;
    frames->height = h;
    frames->source_width = (int)source_w;
    frames->source_height = (int)source_h;
    bool alpha = j->decode_only && transparent(rgba, sw, sh);
    size_t bytes = (size_t)w * h * (alpha ? 4U : 3U) + IMAGE_PIXEL_PADDING;
    if (!store_frame(j, frames, rgba, sw, sh, alpha, bytes, 100))
        goto done;
    image_decode_request encoding = *j;
    encoding.animation = frames;
    ok =
        j->decode_only || encode_pixels(&encoding, frames->pixels[0], frames->width, frames->height,
                                        frames->source_width, frames->source_height, out);
    if (ok && j->retain_static) {
        *animated = frames;
        frames = NULL;
    }
done:
    host.release_frames(frames);
    host.release(rgba);
    if (surface)
        cairo_surface_destroy(surface);
    g_object_unref(svg);
    return ok;
}
static bool decode_file(const image_decode_request *j, graphics_image *out, animation **animated,
                        bool (*cancelled)(void *context), void *context)
{
    memset(out, 0, sizeof(*out));
    *animated = NULL;
    if (cancelled && cancelled(context))
        return false;
    if (!j->video_poster && svg_document(j->path))
        return decode_svg(j, out, animated, cancelled, context);
    AVFormatContext *format = avformat_alloc_context();
    AVCodecContext *decoder = NULL;
    AVFrame *frame = NULL;
    AVPacket *packet = NULL;
    struct SwsContext *scale = NULL;
    unsigned char *rgba = NULL;
    AVDictionary *options = NULL;
    animation *frames = NULL;
    bool ok = false;
    if (!format)
        goto done;
    decode_guard guard = {cancelled, context};
    format->interrupt_callback = (AVIOInterruptCB){decode_interrupt, &guard};
    format->max_streams = j->video_poster ? 16 : 1;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    av_dict_set(&options, "ignore_loop", "1", 0);
    if (avformat_open_input(&format, j->path, NULL, &options) < 0)
        goto done;
    size_t source_limit = j->max_side > 0 && j->max_side <= 4096
                              ? (size_t)j->max_side * (size_t)j->max_side
                              : IMAGE_PIXELS;
    unsigned streams = format->nb_streams;
    if (streams > 16)
        goto done;
    AVDictionary *decode_options[16] = {0};
    for (unsigned i = 0; i < streams; ++i) {
        av_dict_set_int(&decode_options[i], "max_pixels", (int64_t)source_limit, 0);
        av_dict_set(&decode_options[i], "threads", "1", 0);
    }
    int info = avformat_find_stream_info(format, decode_options);
    for (unsigned i = 0; i < streams; ++i)
        av_dict_free(&decode_options[i]);
    if (info < 0)
        goto done;
    int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (stream < 0)
        goto done;
    AVCodecParameters *parameters = format->streams[stream]->codecpar;
    /* Resolve the newer WebP animation codec at runtime so builds against
     * older FFmpeg headers keep working without allowing arbitrary codecs. */
    const AVCodec *webp_animation = avcodec_find_decoder_by_name("webp_anim");
    bool animated_image = parameters->codec_id == AV_CODEC_ID_GIF ||
                          parameters->codec_id == AV_CODEC_ID_APNG ||
                          (webp_animation && parameters->codec_id == webp_animation->id);
    if (!j->video_poster && parameters->codec_id != AV_CODEC_ID_MJPEG &&
        parameters->codec_id != AV_CODEC_ID_PNG && parameters->codec_id != AV_CODEC_ID_WEBP &&
        !animated_image)
        goto done;
    if (parameters->width < 1 || parameters->height < 1 ||
        (size_t)parameters->width > IMAGE_PIXELS / (size_t)parameters->height)
        goto done;
    if (j->max_side && (parameters->width > j->max_side || parameters->height > j->max_side))
        goto done;
    const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec || !(decoder = avcodec_alloc_context3(codec)))
        goto done;
    decoder->max_pixels = source_limit;
    decoder->thread_count = 1;
    if (avcodec_parameters_to_context(decoder, parameters) < 0 ||
        avcodec_open2(decoder, codec, NULL) < 0)
        goto done;
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    frames = host.allocate_zero(1, sizeof(*frames));
    if (frames)
        atomic_init(&frames->references, 1);
    if (!frame || !packet || !frames)
        goto done;
    bool static_only = j->static_only || j->first_frame || j->video_poster;
    int read_status = 0;
    for (unsigned n = 0; n < 4096; ++n) {
        read_status = av_read_frame(format, packet);
        if (read_status < 0 && (read_status != AVERROR_EOF || !j->video_poster))
            break;
        if (decode_interrupt(&guard))
            goto done;
        if (read_status >= 0 && packet->stream_index != stream) {
            av_packet_unref(packet);
            continue;
        }
        int64_t duration = av_rescale_q(packet->duration, format->streams[stream]->time_base,
                                        (AVRational){1, 1000});
        if (avcodec_send_packet(decoder, read_status == AVERROR_EOF ? NULL : packet) < 0)
            goto done;
        av_packet_unref(packet);
        int decoded = avcodec_receive_frame(decoder, frame);
        /* GIF extensions can form packets without producing an image. */
        if (decoded == AVERROR(EAGAIN)) {
            if (read_status == AVERROR_EOF)
                break;
            continue;
        }
        if (decoded < 0)
            goto done;
        if (frame->width < 1 || frame->height < 1 ||
            (size_t)frame->width > IMAGE_PIXELS / (size_t)frame->height)
            goto done;
        if (!frames->count) {
            int w = j->width, h = j->height;
            if (!graphics_image_capacity(w, h))
                goto done;
            double ratio = j->cover ? fmax((double)w / frame->width, (double)h / frame->height)
                                    : fmin((double)w / frame->width, (double)h / frame->height);
            if (j->native && ratio > 1)
                ratio = 1;
            int sw = (int)(frame->width * ratio), sh = (int)(frame->height * ratio);
            if (sw < 1)
                sw = 1;
            if (sh < 1)
                sh = 1;
            if (j->native) {
                w = sw;
                h = sh;
            }
            frames->width = w;
            frames->height = h;
            frames->source_width = frame->width;
            frames->source_height = frame->height;
            size_t rgba_bytes = (size_t)sw * (size_t)sh * 4;
            rgba = host.allocate_zero(1, rgba_bytes + IMAGE_PIXEL_PADDING);
            scale = sws_getContext(frame->width, frame->height, frame->format, sw, sh,
                                   AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
            if (!rgba || !scale)
                goto done;
        }
        if (frame->width != frames->source_width || frame->height != frames->source_height ||
            frames->count == ANIMATION_FRAMES)
            goto done;
        int sw = 0, sh = 0;
        double ratio =
            j->cover ? fmax((double)j->width / frame->width, (double)j->height / frame->height)
                     : fmin((double)j->width / frame->width, (double)j->height / frame->height);
        if (j->native && ratio > 1)
            ratio = 1;
        sw = (int)(frame->width * ratio);
        sh = (int)(frame->height * ratio);
        if (sw < 1)
            sw = 1;
        if (sh < 1)
            sh = 1;
        if (!rgba || !scale)
            goto done;
        unsigned char *dest[] = {rgba, NULL, NULL, NULL};
        int stride[] = {sw * 4, 0, 0, 0};
        if (sws_scale(scale, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height,
                      dest, stride) != sh)
            goto done;
        bool alpha = false;
        if (j->decode_only)
            for (int y = 0; y < sh && !alpha; ++y)
                for (int x = 0; x < sw; ++x)
                    if (rgba[((size_t)y * sw + x) * 4 + 3] != 255) {
                        alpha = true;
                        break;
                    }
        size_t channels = alpha ? 4U : 3U;
        size_t bytes = (size_t)frames->width * frames->height * channels + IMAGE_PIXEL_PADDING;
        if (bytes > ANIMATION_BYTES || frames->bytes > ANIMATION_BYTES - bytes) {
            static_only = true;
            if (frames->count) {
                for (size_t i = 1; i < frames->count; ++i) {
                    host.release(frames->pixels[i]);
                    frames->pixels[i] = NULL;
                }
                frames->count = 1;
                frames->bytes =
                    (size_t)frames->width * frames->height * (frames->rgba[0] ? 4U : 3U) +
                    IMAGE_PIXEL_PADDING;
                frames->duration = frames->delay[0];
                break;
            }
        }
        uint32_t delay = duration > 0 && duration <= 60000 ? (uint32_t)duration : 100;
        if (!store_frame(j, frames, rgba, sw, sh, alpha, bytes, delay))
            goto done;
        if (!animated_image || static_only)
            break;
    }
    if (!frames->count || (animated_image && !static_only && read_status != AVERROR_EOF) ||
        decode_interrupt(&guard))
        goto done;
    image_decode_request encoding = *j;
    encoding.animation = frames;
    ok =
        j->decode_only || encode_pixels(&encoding, frames->pixels[0], frames->width, frames->height,
                                        frames->source_width, frames->source_height, out);
    if (ok && (j->retain_static || frames->count > 1)) {
        *animated = frames;
        frames = NULL;
    }
done:
    host.release_frames(frames);
    av_dict_free(&options);
    sws_freeContext(scale);
    host.release(rgba);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    return ok;
}
static bool encode_frame(const image_decode_request *request, graphics_image *out)
{
    animation *a = request->animation;
    if (!a || request->frame >= a->count)
        return false;
    int w = request->width, h = request->height;
    if (!graphics_image_capacity(w, h))
        return false;
    double ratio = request->cover ? fmax((double)w / a->source_width, (double)h / a->source_height)
                                  : fmin((double)w / a->source_width, (double)h / a->source_height);
    if (request->native && ratio > 1)
        ratio = 1;
    int sw = (int)fmax(1, a->source_width * ratio), sh = (int)fmax(1, a->source_height * ratio);
    if (!graphics_image_capacity(sw, sh))
        return false;
    if (request->native) {
        w = sw;
        h = sh;
    }
    if (w == a->width && h == a->height && !a->rgba[request->frame] && !request->cover)
        return encode_pixels(request, a->pixels[request->frame], w, h, a->source_width,
                             a->source_height, out);
    bool alpha_out = a->rgba[request->frame] && keeps_alpha(request);
    size_t output_channels = alpha_out ? 4U : 3U;
    unsigned char *pixels = host.allocate_zero(
        1, alpha_out ? (size_t)w * h * 4U + IMAGE_PIXEL_PADDING : image_frame_bytes(w, h));
    unsigned char *scaled = host.allocate_zero(1, (size_t)sw * sh * 4 + IMAGE_PIXEL_PADDING);
    if (!pixels || !scaled) {
        host.release(pixels);
        host.release(scaled);
        return false;
    }
    int channels = a->rgba[request->frame] ? 4 : 3;
    struct SwsContext *scale =
        sws_getContext(a->width, a->height, channels == 4 ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24, sw,
                       sh, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    const uint8_t *src[] = {a->pixels[request->frame], NULL, NULL, NULL};
    uint8_t *dst[] = {scaled, NULL, NULL, NULL};
    int strides[] = {a->width * channels, 0, 0, 0}, dest_strides[] = {sw * 4, 0, 0, 0};
    bool ok = scale && sws_scale(scale, src, strides, 0, a->height, dst, dest_strides) == sh;
    uint32_t background = request->matte ? request->background : 0x000000;
    if (ok)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int px = x - (request->center ? (w - sw) / 2 : 0);
                int py = y - (request->center ? (h - sh) / 2 : 0);
                bool inside = px >= 0 && py >= 0 && px < sw && py < sh;
                size_t source = inside ? ((size_t)py * sw + px) * 4 : 0,
                       at = ((size_t)y * w + x) * output_channels;
                unsigned alpha = inside ? scaled[source + 3] : 0;
                if (alpha_out) {
                    /* Outside the scaled image stays fully transparent. */
                    if (inside)
                        memcpy(pixels + at, scaled + source, 4);
                    continue;
                }
                for (int c = 0; c < 3; ++c) {
                    unsigned color = inside ? scaled[source + (size_t)c] : 0;
                    unsigned bg = (background >> (16 - 8 * c)) & 255;
                    pixels[at + (size_t)c] =
                        (unsigned char)((color * alpha + bg * (255 - alpha)) / 255);
                }
            }
    if (ok)
        ok =
            alpha_out
                ? encode_alpha_pixels(request, pixels, w, h, a->source_width, a->source_height, out)
                : encode_pixels(request, pixels, w, h, a->source_width, a->source_height, out);
    sws_freeContext(scale);
    host.release(scaled);
    host.release(pixels);
    return ok;
}

G_MODULE_EXPORT const image_codec_api *papo_image_backend(const image_codec_host *callbacks)
{
    if (!callbacks || callbacks->abi != PAPO_MEDIA_BACKEND_ABI ||
        callbacks->size != sizeof(*callbacks) ||
        callbacks->request_size != sizeof(image_decode_request) ||
        callbacks->animation_size != sizeof(animation) ||
        callbacks->graphics_size != sizeof(graphics_image) || !callbacks->encode ||
        !callbacks->preview || !callbacks->region || !callbacks->encode_rgba ||
        !callbacks->release_frames || !callbacks->allocate || !callbacks->allocate_zero ||
        !callbacks->release)
        return NULL;
    host = *callbacks;
    av_log_set_level(AV_LOG_QUIET);
    static const image_codec_api api = {PAPO_MEDIA_BACKEND_ABI, sizeof(image_codec_api),
                                        decode_file, encode_frame};
    return &api;
}
