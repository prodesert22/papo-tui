#include "model/attachment.h"
#include "platform/runtime.h"
#include <glib/gstdio.h>
#include <stdio.h>

struct attachment_file {
    gint refs;
    char *path, *name, *mime;
    guint64 id, size;
};
static GMutex storage_lock;
static guint64 storage_used, next_id;

static attachment_file *import_stream(GInputStream *in, guint64 size, const char *name,
                                      GCancellable *cancel, GError **error)
{
    g_mutex_lock(&storage_lock);
    bool room = size <= ATTACHMENT_STORAGE_LIMIT - storage_used && next_id != G_MAXUINT64;
    guint64 id = room ? ++next_id : 0;
    if (room)
        storage_used += size;
    g_mutex_unlock(&storage_lock);
    if (!room) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Limite local de anexos: 512 MiB entre rascunhos e envios.");
        return NULL;
    }
    attachment_file *file = g_new0(attachment_file, 1);
    file->refs = 1;
    file->id = id;
    file->size = size;
    file->name = g_filename_display_name(name);
    /* Names are displayed through text_label; also exclude MIME header controls. */
    for (char *p = file->name; *p; ++p)
        if ((unsigned char)*p < 32 || *p == 127)
            *p = '_';
    char *type = g_content_type_guess(file->name, NULL, 0, NULL);
    file->mime = type ? g_content_type_get_mime_type(type) : NULL;
    g_free(type);
    if (!file->mime)
        file->mime = g_strdup("application/octet-stream");
    char *directory = g_build_filename(g_get_tmp_dir(), "papo-attachment-XXXXXX", NULL);
    if (!papo_mkdtemp(directory)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Não foi possível preparar o anexo temporário.");
        g_free(directory);
        attachment_unref(file);
        return NULL;
    }
    file->path = g_build_filename(directory, "data", NULL);
    GFile *target = g_file_new_for_path(file->path);
    GFileOutputStream *out = g_file_create(target, G_FILE_CREATE_PRIVATE, cancel, error);
    g_object_unref(target);
    g_free(directory);
    bool ok = out != NULL;
    guint64 copied = 0;
    guint8 buffer[65536];
    while (ok) {
        gssize n = g_input_stream_read(in, buffer, sizeof(buffer), cancel, error);
        if (n < 0) {
            ok = false;
            break;
        }
        if (!n)
            break;
        if ((guint64)n > size - copied) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "O arquivo mudou durante a preparação; selecione novamente.");
            ok = false;
            break;
        }
        ok = g_output_stream_write_all(G_OUTPUT_STREAM(out), buffer, (gsize)n, NULL, cancel, error);
        copied += (guint64)n;
    }
    if (out) {
        if (ok)
            ok = g_output_stream_close(G_OUTPUT_STREAM(out), cancel, error);
        g_object_unref(out);
    }
    if (ok && copied != size) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "O arquivo mudou durante a preparação; selecione novamente.");
        ok = false;
    }
    if (!ok) {
        attachment_unref(file);
        return NULL;
    }
    return file;
}

attachment_file *attachment_import(const char *path, GCancellable *cancel, GError **error)
{
    GFile *source = g_file_new_for_path(path);
    GFileInfo *kind = g_file_query_info(source, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NONE, cancel, error);
    if (!kind || g_file_info_get_file_type(kind) != G_FILE_TYPE_REGULAR) {
        if (kind) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                                "Selecione um arquivo regular.");
            g_object_unref(kind);
        }
        g_object_unref(source);
        return NULL;
    }
    g_object_unref(kind);
    GFileInputStream *in = g_file_read(source, cancel, error);
    GFileInfo *info = in ? g_file_input_stream_query_info(in,
                                                          G_FILE_ATTRIBUTE_STANDARD_TYPE
                                                          "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                          cancel, error)
                         : NULL;
    attachment_file *result = NULL;
    if (info) {
        if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                                "Selecione um arquivo regular.");
        else {
            char *name = g_file_get_basename(source);
            result = import_stream(G_INPUT_STREAM(in), (guint64)g_file_info_get_size(info), name,
                                   cancel, error);
            g_free(name);
        }
        g_object_unref(info);
    }
    g_clear_object(&in);
    g_object_unref(source);
    return result;
}

attachment_file *attachment_import_bytes(GBytes *bytes, const char *name, GCancellable *cancel,
                                         GError **error)
{
    GInputStream *in = g_memory_input_stream_new_from_bytes(bytes);
    attachment_file *result = import_stream(in, g_bytes_get_size(bytes), name, cancel, error);
    g_object_unref(in);
    return result;
}
attachment_file *attachment_ref(attachment_file *file)
{
    if (file)
        g_atomic_int_inc(&file->refs);
    return file;
}
void attachment_unref(attachment_file *file)
{
    if (!file || !g_atomic_int_dec_and_test(&file->refs))
        return;
    if (file->path) {
        char *parent = g_path_get_dirname(file->path);
        g_unlink(file->path);
        g_rmdir(parent);
        g_free(parent);
    }
    g_mutex_lock(&storage_lock);
    storage_used -= file->size;
    g_mutex_unlock(&storage_lock);
    g_free(file->path);
    g_free(file->name);
    g_free(file->mime);
    g_free(file);
}
const char *attachment_path(const attachment_file *file)
{
    return file->path;
}
const char *attachment_name(const attachment_file *file)
{
    return file->name;
}
const char *attachment_mime(const attachment_file *file)
{
    return file->mime;
}
guint64 attachment_id(const attachment_file *file)
{
    return file->id;
}
guint64 attachment_size(const attachment_file *file)
{
    return file->size;
}
