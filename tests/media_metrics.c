#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
/* Optional instrumentation for the development benchmark only. */
#include <chafa.h>
#include <libavcodec/avcodec.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static atomic_uint decoders, encoders;
static _Atomic size_t payload_bytes;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int (*decode_open)(AVCodecContext *, const AVCodec *, AVDictionary **);
static GString *(*encode_print)(ChafaCanvas *, ChafaTermInfo *);
static void resolve(void)
{
    void *symbol = dlsym(RTLD_NEXT, "avcodec_open2"); memcpy(&decode_open, &symbol, sizeof(decode_open));
    symbol = dlsym(RTLD_NEXT, "chafa_canvas_print"); memcpy(&encode_print, &symbol, sizeof(encode_print));
}
int avcodec_open2(AVCodecContext *context, const AVCodec *codec, AVDictionary **options)
{
    pthread_once(&once, resolve); atomic_fetch_add(&decoders, 1);
    return decode_open ? decode_open(context, codec, options) : -1;
}
GString *chafa_canvas_print(ChafaCanvas *canvas, ChafaTermInfo *info)
{
    pthread_once(&once, resolve); atomic_fetch_add(&encoders, 1);
    GString *result = encode_print ? encode_print(canvas, info) : NULL;
    if (result) atomic_fetch_add(&payload_bytes, result->len);
    return result;
}
__attribute__((destructor)) static void report(void)
{
    const char *path = getenv("PAPO_MEDIA_METRICS");
    FILE *file = path ? fopen(path, "w") : NULL;
    if (file) {
        fprintf(file, "{\"avcodec_open2_calls\":%u,\"graphic_encodes\":%u,\"graphic_payload_bytes\":%zu}\n", atomic_load(&decoders), atomic_load(&encoders), atomic_load(&payload_bytes));
        fclose(file);
    }
}
