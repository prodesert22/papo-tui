#ifndef PAPO_SELF_INSTALL_H
#define PAPO_SELF_INSTALL_H

#include "platform/install.h"
#include <gio/gio.h>
#include <stdbool.h>

/* Replacing this copy of the program with a newer one, for the two kinds of
 * installation that belong to the user: the AppImage file and the folder
 * unpacked from the portable archive. Every other kind is left to its package
 * manager. Nothing here runs on Windows, where these return "cannot". */

/* What a newer version replaces: the AppImage file, or the folder that holds
 * bin/. NULL when this copy is not of that kind or the place cannot be
 * written by this user. The AppImage counts only when this program runs from
 * its mount, and the folder only when it carries the archive's marker. Owned
 * by the caller (g_free). */
char *self_install_target(install_channel channel);
/* A new private folder beside target, on its file system, where the newer
 * version is prepared; NULL when it cannot be created. Owned by the caller. */
char *self_install_staging(const char *target);
/* Worker thread. Unpacks the portable archive into a new folder of staging
 * and accepts it only as the tree of that version: one top folder named
 * papo-tui-<version>, the program at bin/papo-tui, the archive's marker, and
 * no link or entry that reaches outside. Returns the top folder (g_free), or
 * NULL with nothing of it left behind. */
char *self_install_unpack(const char *archive, const char *staging, const char *version,
                          GCancellable *cancel);
/* Puts file in the place of the AppImage at target, in one step, keeping the
 * permissions the old file had. */
bool self_install_replace_file(const char *file, const char *target);
/* Puts the unpacked tree in the place of the folder at target. Refused when the
 * folder holds a name the new tree does not have, at its top or one level
 * down: it is then shared with something else. The old folder is kept beside
 * the new one as "<target>.previous"; when the new one cannot take the place,
 * the old one is put back. */
bool self_install_replace_tree(const char *tree, const char *target);
/* Removes "<prefix>.previous", left by the last replacement, when it is a
 * copy of this program. */
void self_install_forget_previous(const char *prefix);
/* Removes a staging folder and what is in it. */
void self_install_discard(const char *staging);

#endif
