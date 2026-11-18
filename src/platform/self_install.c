#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/self_install.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#ifdef _WIN32
char *self_install_target(install_channel channel)
{
    (void)channel;
    return NULL;
}
char *self_install_staging(const char *target)
{
    (void)target;
    return NULL;
}
char *self_install_unpack(const char *archive, const char *staging, const char *version,
                          GCancellable *cancel)
{
    (void)archive;
    (void)staging;
    (void)version;
    (void)cancel;
    return NULL;
}
bool self_install_replace_file(const char *file, const char *target)
{
    (void)file;
    (void)target;
    return false;
}
bool self_install_replace_tree(const char *tree, const char *target)
{
    (void)tree;
    (void)target;
    return false;
}
void self_install_forget_previous(const char *prefix)
{
    (void)prefix;
}
void self_install_discard(const char *staging)
{
    (void)staging;
}
#else
#include "platform/runtime.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define STAGING_PATTERN ".papo-tui-update-XXXXXX"
#define UNPACKED "tree"
#define PREVIOUS ".previous"
/* Deeper than any tree the packages have. */
#define DEPTH_LIMIT 32

static bool marked_tarball(const char *prefix)
{
    char *path = g_build_filename(prefix, INSTALL_MARKER_DIRECTORY, INSTALL_MARKER_FILE, NULL);
    char *text = NULL;
    gsize length = 0;
    bool marked = g_file_get_contents(path, &text, &length, NULL) && length < 64 &&
                  strlen(text) == length &&
                  install_channel_parse(g_strstrip(text)) == INSTALL_TARBALL;
    g_free(text);
    g_free(path);
    return marked;
}
/* The folder that holds path, when this user can create and rename in it. */
static bool parent_writable(const char *path)
{
    char *parent = g_path_get_dirname(path);
    bool writable = g_access(parent, W_OK | X_OK) == 0;
    g_free(parent);
    return writable;
}
char *self_install_target(install_channel channel)
{
    char *prefix = install_prefix(), *target = NULL;
    if (!prefix)
        return NULL;
    if (channel == INSTALL_APPIMAGE) {
        /* A terminal that is itself an AppImage passes its own variables on:
         * the file is this program's only when it runs from that mount. */
        const char *image = getenv("APPIMAGE"), *mount = getenv("APPDIR");
        struct stat status;
        if (image && g_path_is_absolute(image) && mount && *mount &&
            g_str_has_prefix(prefix, mount) &&
            (prefix[strlen(mount)] == '/' || !prefix[strlen(mount)]) &&
            g_lstat(image, &status) == 0 && S_ISREG(status.st_mode) && parent_writable(image))
            target = g_strdup(image);
    } else if (channel == INSTALL_TARBALL) {
        struct stat status;
        if (marked_tarball(prefix) && g_lstat(prefix, &status) == 0 && S_ISDIR(status.st_mode) &&
            parent_writable(prefix))
            target = g_strdup(prefix);
    }
    g_free(prefix);
    return target;
}
char *self_install_staging(const char *target)
{
    if (!target || !g_path_is_absolute(target))
        return NULL;
    char *parent = g_path_get_dirname(target);
    char *staging = g_build_filename(parent, STAGING_PATTERN, NULL);
    g_free(parent);
    if (!papo_mkdtemp(staging)) {
        g_free(staging);
        return NULL;
    }
    return staging;
}
/* Removes a tree without following links; depth bounds the recursion. */
static void remove_tree(const char *path, int depth)
{
    struct stat status;
    if (g_lstat(path, &status))
        return;
    if (S_ISDIR(status.st_mode) && depth < DEPTH_LIMIT) {
        /* A folder unpacked without write permission still has to go. */
        g_chmod(path, 0700);
        GDir *directory = g_dir_open(path, 0, NULL);
        const char *name;
        while (directory && (name = g_dir_read_name(directory))) {
            char *child = g_build_filename(path, name, NULL);
            remove_tree(child, depth + 1);
            g_free(child);
        }
        if (directory)
            g_dir_close(directory);
        g_rmdir(path);
    } else
        g_unlink(path);
}
void self_install_discard(const char *staging)
{
    char *name = staging ? g_path_get_basename(staging) : NULL;
    /* Only folders this module made are ever removed whole. */
    if (name && g_str_has_prefix(name, ".papo-tui-update-"))
        remove_tree(staging, 0);
    g_free(name);
}
/* Every entry below root is a folder, a regular file or a link that stays
 * inside root. */
static bool contained(const char *root, const char *path, int depth)
{
    struct stat status;
    if (depth >= DEPTH_LIMIT || g_lstat(path, &status))
        return false;
    if (S_ISLNK(status.st_mode)) {
        char *link = g_file_read_link(path, NULL);
        char *folder = g_path_get_dirname(path);
        char *resolved = link ? g_canonicalize_filename(link, folder) : NULL;
        size_t length = strlen(root);
        bool inside = resolved && !strncmp(resolved, root, length) && resolved[length] == '/';
        g_free(resolved);
        g_free(folder);
        g_free(link);
        return inside;
    }
    if (S_ISREG(status.st_mode))
        return true;
    if (!S_ISDIR(status.st_mode))
        return false;
    GDir *directory = g_dir_open(path, 0, NULL);
    const char *name;
    bool ok = directory != NULL;
    while (ok && (name = g_dir_read_name(directory))) {
        char *child = g_build_filename(path, name, NULL);
        ok = contained(root, child, depth + 1);
        g_free(child);
    }
    if (directory)
        g_dir_close(directory);
    return ok;
}
char *self_install_unpack(const char *archive, const char *staging, const char *version,
                          GCancellable *cancel)
{
    char *folder = g_build_filename(staging, UNPACKED, NULL);
    char *expected = g_strconcat("papo-tui-", version, NULL);
    char *root = g_build_filename(folder, expected, NULL);
    char *program = g_build_filename(root, "bin", "papo-tui", NULL);
    bool ok = g_mkdir(folder, 0700) == 0;
    if (ok) {
        /* The system's tar, as an argument vector: no shell reads the names. */
        GSubprocess *tar =
            g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             NULL, "tar", "-xzf", archive, "-C", folder, "--no-same-owner", NULL);
        ok = tar && g_subprocess_wait_check(tar, cancel, NULL);
        if (tar && !ok)
            g_subprocess_force_exit(tar);
        g_clear_object(&tar);
    }
    /* One top folder, of the expected name, and nothing beside it. */
    GDir *directory = ok ? g_dir_open(folder, 0, NULL) : NULL;
    const char *name;
    unsigned entries = 0;
    while (directory && (name = g_dir_read_name(directory)))
        ok = ok && ++entries == 1 && !strcmp(name, expected);
    if (directory)
        g_dir_close(directory);
    struct stat status;
    ok = ok && entries == 1 && g_lstat(root, &status) == 0 && S_ISDIR(status.st_mode) &&
         g_lstat(program, &status) == 0 && S_ISREG(status.st_mode) && (status.st_mode & 0100) &&
         marked_tarball(root) && contained(root, root, 0);
    if (!ok) {
        remove_tree(folder, 0);
        g_clear_pointer(&root, g_free);
    }
    g_free(program);
    g_free(expected);
    g_free(folder);
    return root;
}
bool self_install_replace_file(const char *file, const char *target)
{
    struct stat old, fresh;
    if (g_lstat(target, &old) || !S_ISREG(old.st_mode) || g_lstat(file, &fresh) ||
        !S_ISREG(fresh.st_mode))
        return false;
    /* The new file is ready, with the old one's permissions, before it takes
     * the name: there is never a moment without a program there. */
    if (g_chmod(file, old.st_mode & 07777))
        return false;
    return g_rename(file, target) == 0;
}
/* Every name in the old folder, and in each folder directly inside it, is also
 * in the new tree. A folder that holds anything else, such as ~/.local with the
 * archive unpacked over it, is shared and is never replaced whole. */
static bool only_program(const char *target, const char *tree)
{
    GDir *top = g_dir_open(target, 0, NULL);
    const char *name;
    bool ok = top != NULL;
    while (ok && (name = g_dir_read_name(top))) {
        char *old = g_build_filename(target, name, NULL);
        char *fresh = g_build_filename(tree, name, NULL);
        struct stat had, has;
        ok = g_lstat(old, &had) == 0 && g_lstat(fresh, &has) == 0;
        if (ok && S_ISDIR(had.st_mode)) {
            GDir *inner = S_ISDIR(has.st_mode) ? g_dir_open(old, 0, NULL) : NULL;
            const char *child;
            ok = inner != NULL;
            while (ok && (child = g_dir_read_name(inner))) {
                char *expected = g_build_filename(fresh, child, NULL);
                ok = g_lstat(expected, &has) == 0;
                g_free(expected);
            }
            if (inner)
                g_dir_close(inner);
        }
        g_free(fresh);
        g_free(old);
    }
    if (top)
        g_dir_close(top);
    return ok;
}
bool self_install_replace_tree(const char *tree, const char *target)
{
    struct stat status;
    if (!marked_tarball(target) || !marked_tarball(tree) || g_lstat(tree, &status) ||
        !S_ISDIR(status.st_mode) || !only_program(target, tree))
        return false;
    char *previous = g_strconcat(target, PREVIOUS, NULL);
    /* An older leftover of this program makes room; anything else stays. */
    self_install_forget_previous(target);
    bool ok = !g_file_test(previous, G_FILE_TEST_EXISTS) && g_rename(target, previous) == 0;
    if (ok && g_rename(tree, target)) {
        /* The new tree could not take the place: the old one goes back. */
        g_rename(previous, target);
        ok = false;
    }
    g_free(previous);
    return ok;
}
void self_install_forget_previous(const char *prefix)
{
    if (!prefix || !g_path_is_absolute(prefix))
        return;
    char *previous = g_strconcat(prefix, PREVIOUS, NULL);
    struct stat status;
    if (g_lstat(previous, &status) == 0 && S_ISDIR(status.st_mode) && marked_tarball(previous))
        remove_tree(previous, 0);
    g_free(previous);
}
#endif
