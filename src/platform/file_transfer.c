#include "platform/file_transfer.h"
static GPtrArray *tasks;
GTask *file_transfer_task_new(GCancellable *cancel)
{
    GTask *task = g_task_new(NULL, cancel, NULL, NULL);
    if (!tasks)
        tasks = g_ptr_array_new_with_free_func(g_object_unref);
    g_ptr_array_add(tasks, g_object_ref(task));
    return task;
}
void file_transfer_tasks_poll(void)
{
    for (int i = 0; i < 16 && g_main_context_iteration(NULL, false); ++i) {
    }
    for (guint i = 0; tasks && i < tasks->len;) {
        if (g_task_get_completed(g_ptr_array_index(tasks, i)))
            g_ptr_array_remove_index_fast(tasks, i);
        else
            ++i;
    }
}
void file_transfer_shutdown(void)
{
    for (guint i = 0; tasks && i < tasks->len; ++i)
        g_cancellable_cancel(g_task_get_cancellable(g_ptr_array_index(tasks, i)));
    while (tasks && tasks->len) {
        file_transfer_tasks_poll();
        if (tasks->len)
            g_usleep(1000);
    }
    g_clear_pointer(&tasks, g_ptr_array_unref);
}
void clipboard_content_free(clipboard_content *content)
{
    if (!content)
        return;
    g_clear_pointer(&content->paths, g_ptr_array_unref);
    g_clear_pointer(&content->image, g_bytes_unref);
    g_free(content->image_name);
    g_free(content->text);
    g_free(content);
}
