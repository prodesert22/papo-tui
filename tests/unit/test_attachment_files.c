#include "model/attachment.h"
#include <glib/gstdio.h>

static void private_snapshot(void)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("papo-attachment-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *source = g_build_filename(directory, "résumé.bin", NULL);
    const char original[] = {'\0', '\xff', 'a'};
    g_assert_true(g_file_set_contents(source, original, sizeof(original), &error));
    attachment_file *file = attachment_import(source, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(file);
    char *path = g_strdup(attachment_path(file));
    char *parent = g_path_get_dirname(path);
    g_assert_cmpstr(attachment_name(file), ==, "résumé.bin");
    g_assert_cmpuint(attachment_size(file), ==, sizeof(original));
    g_assert_true(g_file_set_contents(source, "changed", -1, &error));
    char *data = NULL;
    gsize size;
    g_assert_true(g_file_get_contents(path, &data, &size, &error));
    g_assert_cmpmem(data, size, original, sizeof(original));
    g_free(data);
    attachment_ref(file);
    attachment_unref(file);
    g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
    attachment_unref(file);
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(parent, G_FILE_TEST_EXISTS));
    g_unlink(source);
    g_rmdir(directory);
    g_free(path);
    g_free(parent);
    g_free(source);
    g_free(directory);
}
static void failures_and_cancellation(void)
{
    GError *error = NULL;
    attachment_file *file = attachment_import(g_get_tmp_dir(), NULL, &error);
    g_assert_null(file);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE);
    g_clear_error(&error);
    /* No allocation or read is needed before the storage admission decision. */
    GBytes *oversized = g_bytes_new_static("", ATTACHMENT_STORAGE_LIMIT + 1);
    file = attachment_import_bytes(oversized, "large.bin", NULL, &error);
    g_assert_null(file);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&error);
    g_bytes_unref(oversized);
    GBytes *bytes = g_bytes_new_static("test", 4);
    GCancellable *cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    file = attachment_import_bytes(bytes, "cancelled.bin", cancel, &error);
    g_assert_null(file);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_object_unref(cancel);
    g_bytes_unref(bytes);
    bytes = g_bytes_new_static("", 0);
    file = attachment_import_bytes(bytes, "empty.txt", NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(file);
    g_assert_cmpuint(attachment_size(file), ==, 0);
    attachment_unref(file);
    g_bytes_unref(bytes);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/attachments/private-snapshot", private_snapshot);
    g_test_add_func("/attachments/failures-cancellation", failures_and_cancellation);
    return g_test_run();
}
