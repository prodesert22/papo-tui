#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/self_install.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static char *path(const char *a, const char *b)
{
    return g_build_filename(a, b, NULL);
}
static bool put(const char *root, const char *relative, const char *text, int mode)
{
    char *file = path(root, relative), *folder = g_path_get_dirname(file);
    bool ok = !g_mkdir_with_parents(folder, 0755) && g_file_set_contents(file, text, -1, NULL) &&
              !g_chmod(file, mode);
    g_free(folder);
    g_free(file);
    return ok;
}
static bool holds(const char *root, const char *relative, const char *text)
{
    char *file = path(root, relative), *found = NULL;
    bool same = g_file_get_contents(file, &found, NULL, NULL) && !strcmp(found, text);
    g_free(found);
    g_free(file);
    return same;
}
/* A tree as the portable archive has it, under work/<top>. */
static bool tree(const char *work, const char *top, const char *word, int program_mode)
{
    char *root = path(work, top);
    bool ok = put(root, "bin/papo-tui", "program", program_mode) &&
              put(root, "lib/papo-tui/libx.so", "library", 0644);
    if (word)
        ok = ok && put(root, INSTALL_MARKER_DIRECTORY "/" INSTALL_MARKER_FILE, word, 0644);
    g_free(root);
    return ok;
}
/* Packs every entry of work into archive, with the system's tar. */
static bool pack(const char *work, const char *archive)
{
    GPtrArray *arguments = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(arguments, g_strdup("tar"));
    g_ptr_array_add(arguments, g_strdup("-czf"));
    g_ptr_array_add(arguments, g_strdup(archive));
    g_ptr_array_add(arguments, g_strdup("-C"));
    g_ptr_array_add(arguments, g_strdup(work));
    GDir *directory = g_dir_open(work, 0, NULL);
    const char *name;
    while (directory && (name = g_dir_read_name(directory)))
        g_ptr_array_add(arguments, g_strdup(name));
    if (directory)
        g_dir_close(directory);
    g_ptr_array_add(arguments, NULL);
    int status = -1;
    bool ok = g_spawn_sync(NULL, (char **)arguments->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                           NULL, NULL, &status, NULL) &&
              status == 0;
    g_ptr_array_unref(arguments);
    return ok;
}
static void wipe(const char *folder)
{
    char *command[] = {"rm", "-rf", (char *)folder, NULL};
    g_spawn_sync(NULL, command, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
}
/* Unpacks what build() put under a fresh work folder; the returned root is
 * NULL when the archive was refused. */
typedef bool (*builder)(const char *work);
static char *unpacked(const char *base, const char *label, builder build, char **staging)
{
    char *work = path(base, label), *target = path(base, "installed");
    char *archive = g_strconcat(work, ".tar.gz", NULL);
    char *root = NULL;
    *staging = NULL;
    if (!g_mkdir(work, 0755) && build(work) && pack(work, archive) &&
        (*staging = self_install_staging(target)))
        root = self_install_unpack(archive, *staging, "9.9.9", NULL);
    g_free(archive);
    g_free(target);
    g_free(work);
    return root;
}
static bool good(const char *work)
{
    char *root = path(work, "papo-tui-9.9.9"), *alias = path(root, "bin/alias");
    bool ok = tree(work, "papo-tui-9.9.9", "tarball\n", 0755) && !symlink("papo-tui", alias);
    g_free(alias);
    g_free(root);
    return ok;
}
static bool other_name(const char *work)
{
    return tree(work, "papo-tui-9.9.8", "tarball\n", 0755);
}
static bool two_tops(const char *work)
{
    return tree(work, "papo-tui-9.9.9", "tarball\n", 0755) && put(work, "extra.txt", "x", 0644);
}
static bool no_program(const char *work)
{
    char *program = path(work, "papo-tui-9.9.9/bin/papo-tui");
    bool ok = tree(work, "papo-tui-9.9.9", "tarball\n", 0755) && !g_unlink(program);
    g_free(program);
    return ok;
}
static bool not_executable(const char *work)
{
    return tree(work, "papo-tui-9.9.9", "tarball\n", 0644);
}
static bool no_marker(const char *work)
{
    return tree(work, "papo-tui-9.9.9", NULL, 0755);
}
static bool other_marker(const char *work)
{
    return tree(work, "papo-tui-9.9.9", "appimage\n", 0755);
}
static bool link_outside(const char *work)
{
    char *link = path(work, "papo-tui-9.9.9/share/secret");
    bool ok = tree(work, "papo-tui-9.9.9", "tarball\n", 0755) && !symlink("/etc/passwd", link);
    g_free(link);
    return ok;
}
static bool link_above(const char *work)
{
    char *link = path(work, "papo-tui-9.9.9/bin/up");
    bool ok = tree(work, "papo-tui-9.9.9", "tarball\n", 0755) && !symlink("../../outside", link);
    g_free(link);
    return ok;
}
static bool program_is_link(const char *work)
{
    char *program = path(work, "papo-tui-9.9.9/bin/papo-tui");
    bool ok = tree(work, "papo-tui-9.9.9", "tarball\n", 0755) && !g_unlink(program) &&
              !symlink("../lib/papo-tui/libx.so", program);
    g_free(program);
    return ok;
}

int main(void)
{
    char *base = g_dir_make_tmp("papo-self-install-XXXXXX", NULL);
    CHECK(base);
    char *staging = NULL, *root = NULL;
    struct stat status;

    /* This test program is neither an AppImage nor an unpacked archive, even
     * when a terminal that is one passes its variables on. */
    char *image = path(base, "Terminal.AppImage");
    CHECK(put(base, "Terminal.AppImage", "terminal", 0755));
    g_setenv("APPIMAGE", image, TRUE);
    g_setenv("APPDIR", "/tmp/.mount_terminal", TRUE);
    CHECK(!self_install_target(INSTALL_APPIMAGE));
    CHECK(!self_install_target(INSTALL_TARBALL));
    CHECK(!self_install_target(INSTALL_DEB_UBUNTU));
    CHECK(holds(base, "Terminal.AppImage", "terminal"));

    /* The staging folder is private and beside what it will replace. */
    char *target = path(base, "installed");
    staging = self_install_staging(target);
    CHECK(staging && g_str_has_prefix(staging, base) && !g_lstat(staging, &status));
    CHECK(S_ISDIR(status.st_mode) && (status.st_mode & 0777) == 0700);
    CHECK(!self_install_staging("relative/place"));
    CHECK(!self_install_staging("/nonexistent-papo-folder/installed"));

    /* An AppImage is replaced in one step and keeps the old permissions. */
    CHECK(put(base, "Papo.AppImage", "old image", 0750));
    CHECK(put(staging, "new.AppImage", "new image", 0600));
    char *old_image = path(base, "Papo.AppImage"), *new_image = path(staging, "new.AppImage");
    CHECK(self_install_replace_file(new_image, old_image));
    CHECK(holds(base, "Papo.AppImage", "new image") && !g_lstat(old_image, &status));
    CHECK((status.st_mode & 07777) == 0750 && g_lstat(new_image, &status));
    CHECK(!self_install_replace_file(new_image, old_image)); /* Nothing to put there. */
    CHECK(put(staging, "other.AppImage", "x", 0600));
    char *other = path(staging, "other.AppImage");
    CHECK(!self_install_replace_file(other, base)); /* Not a file. */
    CHECK(!self_install_replace_file(other, "/nonexistent-papo-file"));
    CHECK(holds(base, "Papo.AppImage", "new image"));
    self_install_discard(staging);
    CHECK(g_lstat(staging, &status));
    g_free(staging);

    /* Only the tree of that version, whole and closed, is accepted. */
    root = unpacked(base, "good", good, &staging);
    CHECK(root && g_str_has_suffix(root, "/tree/papo-tui-9.9.9"));
    CHECK(holds(root, "bin/papo-tui", "program") && holds(root, "bin/alias", "program"));
    char *accepted = root, *accepted_staging = staging;
    static const struct {
        const char *label;
        builder build;
    } refused[] = {
        {"other-name", other_name},
        {"two-tops", two_tops},
        {"no-program", no_program},
        {"not-executable", not_executable},
        {"no-marker", no_marker},
        {"other-marker", other_marker},
        {"link-outside", link_outside},
        {"link-above", link_above},
        {"program-is-link", program_is_link},
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        root = unpacked(base, refused[i].label, refused[i].build, &staging);
        char *left = staging ? path(staging, "tree") : NULL;
        if (root || !staging || !g_lstat(left, &status)) {
            fprintf(stderr, "accepted or left behind: %s\n", refused[i].label);
            return 1;
        }
        self_install_discard(staging);
        g_free(left);
        g_free(staging);
        g_free(root);
    }
    staging = self_install_staging(target);
    CHECK(staging &&
          !self_install_unpack("/nonexistent-papo-archive.tar.gz", staging, "9.9.9", NULL));
    CHECK(put(staging, "garbage.tar.gz", "this is not an archive", 0600));
    char *garbage = path(staging, "garbage.tar.gz");
    CHECK(!self_install_unpack(garbage, staging, "9.9.9", NULL));
    self_install_discard(staging);
    g_free(garbage);
    g_free(staging);

    /* The tree takes the folder's place; the old one waits beside it. */
    CHECK(tree(base, "installed", "tarball\n", 0755));
    CHECK(put(target, "lib/papo-tui/old-only.so", "old", 0644));
    char *previous = g_strconcat(target, ".previous", NULL);
    CHECK(self_install_replace_tree(accepted, target));
    CHECK(holds(target, "bin/alias", "program"));
    CHECK(!holds(target, "lib/papo-tui/old-only.so", "old"));
    CHECK(holds(previous, "lib/papo-tui/old-only.so", "old") && g_lstat(accepted, &status));
    /* The next start forgets the old copy, and only a copy of this program. */
    self_install_forget_previous(target);
    CHECK(g_lstat(previous, &status));
    CHECK(put(previous, "notes.txt", "mine", 0644));
    self_install_forget_previous(target);
    CHECK(holds(previous, "notes.txt", "mine"));
    /* A folder of someone else in the way stops the replacement before it starts. */
    root = unpacked(base, "again", good, &staging);
    CHECK(root && !self_install_replace_tree(root, target));
    CHECK(holds(target, "bin/alias", "program") && holds(previous, "notes.txt", "mine"));
    CHECK(holds(root, "bin/papo-tui", "program"));
    /* A folder that holds anything the new tree does not is shared with
     * something else, like ~/.local with the archive unpacked over it: it is
     * never renamed away. */
    static const char *const foreign[] = {"notes.txt", "state/papo-tui/sessions.json",
                                          "bin/other-tool", "share/Trash/info",
                                          "lib/other-library.so"};
    for (size_t i = 0; i < sizeof(foreign) / sizeof(foreign[0]); ++i) {
        char *label = g_strdup_printf("shared-%zu", i), *shared = path(base, label);
        char *kept = g_strconcat(shared, ".previous", NULL);
        CHECK(tree(base, label, "tarball\n", 0755) && put(shared, foreign[i], "theirs", 0644));
        if (self_install_replace_tree(root, shared) || !holds(shared, foreign[i], "theirs") ||
            !holds(shared, "bin/papo-tui", "program") || !g_lstat(kept, &status) ||
            !holds(root, "bin/papo-tui", "program")) {
            fprintf(stderr, "replaced a shared folder: %s\n", foreign[i]);
            return 1;
        }
        g_free(kept);
        g_free(shared);
        g_free(label);
    }
    /* What is replaced must be an unpacked archive too. */
    char *plain = path(base, "plain");
    CHECK(put(plain, "bin/papo-tui", "manual install", 0755));
    CHECK(!self_install_replace_tree(root, plain) &&
          holds(plain, "bin/papo-tui", "manual install"));
    CHECK(!self_install_replace_tree(plain, target));
    self_install_discard(staging);
    self_install_discard(accepted_staging);
    /* Folders this module did not make are never removed whole. */
    self_install_discard(plain);
    CHECK(holds(plain, "bin/papo-tui", "manual install"));

    wipe(base);
    return 0;
}
