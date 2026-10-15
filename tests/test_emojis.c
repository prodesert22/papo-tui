#define _POSIX_C_SOURCE 200809L
#include "model/assets.h"
#include "model/emoji_catalog.h"
#include "ui/icons.h"
#include "ui/message_time.h"
#include "ui/message_blocks.h"
#include "model/history.h"
#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static const char *id = "00000000-0000-4000-8000-000000000001";
static size_t resolve(void *ctx, const uint32_t *text, size_t length, size_t at, uint32_t *icon)
{
    (void)ctx;
    const uint32_t custom[] = {':','c','a','t',':'};
    if (length - at >= 5 && !memcmp(text + at, custom, sizeof(custom))) { *icon = ICON_CUSTOM | 1; return 5; }
    size_t n;
    const unicode_emoji *e = emoji_catalog_match(text + at, length - at, &n);
    if (e) { *icon = (uint32_t)(emoji_catalog_index(e) + 1); return n; }
    return 0;
}
static void sequences(void)
{
    assert(emoji_catalog_count() > 3500);
    assert(!strcmp(emoji_catalog_get(emoji_catalog_order(0))->text, "😀"));
    const char *cases[] = {"😄", "👍🏽", "👨‍👩‍👧‍👦", "🇧🇷", "❤️", "1️⃣"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        text_buffer b; assert(text_decode(&b, cases[i]));
        size_t n; const unicode_emoji *e = emoji_catalog_match(b.data, b.length, &n);
        assert(e && n == b.length && *e->name && *e->english);
        text_icons(&b, resolve, NULL, NULL);
        text_layout l; text_measure(&b, 20, &l);
        assert(l.count == 2 && l.cells[0].icon && l.cells[0].width == 2 && l.cells[0].end == b.length);
        b.cursor = 0; text_horizontal(&b, true); assert(b.cursor == b.length);
        text_delete(&b, true); assert(!b.length);
    }
    text_buffer b; assert(text_decode(&b, "123 # *")); size_t n;
    for (size_t i = 0; i < b.length; ++i) assert(!emoji_catalog_match(b.data + i, b.length - i, &n));
    assert(text_decode(&b, "a:cat:😄z")); text_icons(&b, resolve, NULL, NULL);
    text_layout l; text_measure(&b, 8, &l);
    assert(l.cells[1].start == 1 && l.cells[1].end == 6 && l.cells[1].width == 2);
    b.cursor = 1; text_horizontal(&b, true); assert(b.cursor == 6);
    text_delete(&b, true); char out[TEXT_UTF8_BYTES]; assert(text_encode(&b, out)); assert(!strcmp(out, "a😄z"));
    b.cursor = 1; b.anchor = b.length; b.selecting = true;
    text_buffer insertion; assert(text_decode(&insertion, "👍🏽")); assert(text_insert_buffer(&b, &insertion));
    assert(text_encode(&b, out) && !strcmp(out, "a👍🏽"));
}
static void assets(void)
{
    chat_assets *a = chat_assets_create(); assert(a);
    chat_assets_emojis_request(a, false); assert(chat_assets_emojis_wanted(a));
    json_t *e = json_pack("{s:s,s:s,s:s,s:s,s:s}", "id", id, "name", "gato 界", "format", "PNG",
        "created_at", "2026-10-14T10:00:00+00:00", "image_blob", "AQID");
    json_t *items = json_array(); assert(!json_array_append(items, e));
    json_t *page = json_pack("{s:O,s:b}", "emojis", items, "has_more", 1);
    assert(chat_assets_emojis_response(a, page, 200));
    assert(strstr(chat_assets_emojis_path(a), "%2B00%3A00"));
    assert(chat_assets_emoji_count(a) == 1 && access(chat_assets_emoji(a, 0)->path, R_OK) == 0);
    struct stat permissions; assert(!stat(chat_assets_emoji(a, 0)->path, &permissions) && (permissions.st_mode & 0777) == 0600);
    chat_assets *other = chat_assets_create(); assert(other);
    chat_assets_emojis_request(other, false);
    assert(chat_assets_emojis_response(other, page, 200));
    assert(strcmp(chat_assets_emoji(a, 0)->path, chat_assets_emoji(other, 0)->path));
    char *other_path = strdup(chat_assets_emoji(other, 0)->path); assert(other_path);
    chat_assets_destroy(other); assert(access(other_path,F_OK) != 0); free(other_path);
    assert(!access(chat_assets_emoji(a,0)->path,R_OK));
    chat_assets_avatar_invalidate(a,NULL); chat_assets_avatar_invalidate(a,"invalid");
    /* Repeated cursors fail closed rather than issuing an endless read loop. */
    assert(!chat_assets_emojis_response(a, page, 200)); assert(!chat_assets_emojis_wanted(a));
    chat_assets_emojis_request(a, true); json_object_set_new(page, "has_more", json_false());
    assert(chat_assets_emojis_response(a, page, 200));
    const author_avatar *avatar = chat_assets_avatar(a, id); assert(avatar && !avatar->path);
    json_t *ids = chat_assets_profiles_request(a); assert(json_array_size(ids) == 1);
    json_t *profile = json_pack("{s:s,s:s,s:s}", "id", id, "avatar_blob", "AQID", "avatar_format", "PNG");
    json_t *profiles = json_pack("{s:[O]}", "profiles", profile);
    assert(chat_assets_profiles_response(a, profiles, 200));
    avatar = chat_assets_avatar(a, id); assert(avatar && avatar->path);
    char *path = strdup(avatar->path); assert(path);
    chat_assets_visible_begin(a); chat_assets_avatar_invalidate(a, id);
    assert(!chat_assets_profiles_wanted(a) && access(path, F_OK) != 0); free(path);
    avatar = chat_assets_avatar(a, id); assert(chat_assets_profiles_wanted(a));
    ids = chat_assets_profiles_request(a); assert(json_array_size(ids) == 1);
    chat_assets_cancel(a); assert(chat_assets_profiles_wanted(a));
    ids = chat_assets_profiles_request(a); assert(json_array_size(ids) == 1);
    /* A stale in-flight response must not undo a newer avatar_update. */
    chat_assets_avatar_invalidate(a, id);
    assert(chat_assets_profiles_response(a, profiles, 200)); assert(!chat_assets_avatar(a, id)->path);
    chat_assets_destroy(a); json_decref(profiles); json_decref(profile); json_decref(page); json_decref(items); json_decref(e);
}
static void time_and_header(void)
{
    assert(setenv("TZ", "UTC", 1) == 0); tzset();
    char stamp[128];
    assert(message_time_format("2026-10-14T12:34:56Z", 1791979200, stamp, sizeof(stamp)));
    /* Compare to libc's actual locale output, without assuming a date order. */
    struct tm expected = {.tm_year=126,.tm_mon=9,.tm_mday=14,.tm_hour=12,.tm_min=34,.tm_sec=56};
    time_t now = mktime(&expected); char native[128]; strftime(native, sizeof(native), "%X", &expected);
    assert(message_time_format("2026-10-14T12:34:56Z", now, stamp, sizeof(stamp)) && !strcmp(native, stamp));
    assert(message_time_format("2026-10-14T12:34:56Z", now + 86400, stamp, sizeof(stamp)) && strcmp(native, stamp));
    assert(!message_time_format("invalid", now, stamp, sizeof(stamp)));
    json_t *m = json_pack("{s:s,s:s,s:s,s:s,s:s}", "id", id, "content", "😄 :cat:",
        "created_at", "2026-10-14T12:34:56Z", "edited_at", "2026-10-14T12:35:56Z", "author_id", id);
    message_blocks b;
    assert(message_blocks_build_visual(&b,m,"Author Name","",60,true,NULL,NULL,NULL,NULL,NULL,resolve,NULL,NULL,false));
    assert(b.rows[0].header && b.rows[1].header && b.rows[2].header);
    assert(strstr(b.rows[0].segments[0].text,"editado") && b.rows[0].segments[0].x == 7);
    assert(!b.rows[3].header && b.rows[3].segments[0].x == 0 && b.rows[3].icon_count == 2);
    message_blocks_free(&b);
    json_object_set_new(m, "webhook_id", json_string(id));
    json_object_set_new(m, "author_id", json_null());
    json_object_set_new(m, "external_author", json_pack("{s:s}", "name", "Deploy App"));
    assert(history_message_webhook(m) && !strcmp(history_message_author(NULL, m), "Deploy App"));
    assert(message_blocks_build_visual(&b,m,history_message_author(NULL,m),"",60,true,NULL,NULL,NULL,NULL,NULL,resolve,NULL,NULL,false));
    assert(strstr(b.rows[0].segments[0].text, "Deploy App [APP]"));
    message_blocks_free(&b);
    json_object_del(m, "external_author");
    assert(!strcmp(history_message_author(NULL, m), "Webhook"));
    json_object_set_new(m, "webhook_id", json_string("invalid"));
    assert(!history_message_webhook(m));
    json_decref(m);
}
static void font_render(void)
{
    graphics_terminal t = {.protocol=GRAPHICS_KITTY,.supported=true,.cell_width=8,.cell_height=16};
    graphics_image image = {0};
    assert(icons_render_unicode("👨‍👩‍👧‍👦",16,16,0x15181e,t,77,&image));
    assert(image.data && image.columns == 2 && image.rows == 1 && image.id == 77);
    graphics_image_free(&image);
}
static void grouping(void)
{
    json_t *a = json_pack("{s:s,s:s,s:s,s:s,s:s}", "id", id, "channel_id", id,
        "author_id", id, "content", "First", "created_at", "2026-10-14T12:34:56Z");
    json_t *b = json_deep_copy(a); assert(a && b);
    assert(!message_same_author(NULL, b) && message_same_author(a, b));
    const char *other = "00000000-0000-4000-8000-000000000002";
    json_object_set_new(b, "author_id", json_string(other)); assert(!message_same_author(a, b));
    json_object_set_new(b, "author_id", json_string(id));
    json_object_set_new(b, "channel_id", json_string(other)); assert(!message_same_author(a, b));
    json_object_set_new(b, "channel_id", json_string(id));
    json_object_set_new(b, "created_at", json_string("2026-10-15T12:34:56Z"));
    assert(message_same_author(a, b));
    json_object_set_new(a, "author_id", json_null()); json_object_set_new(b, "author_id", json_null());
    assert(!message_same_author(a, b));
    json_object_set_new(a, "webhook_id", json_string(id)); assert(!message_same_author(a, b));
    json_object_set_new(b, "webhook_id", json_string(id)); assert(message_same_author(a, b));
    json_t *external = json_pack("{s:s,s:s,s:s,s:s}", "source", "discord", "id", "external-id",
                                "name", "App", "avatar_url", "https://example.invalid/avatar.png");
    json_object_set(a, "external_author", external);
    json_object_set_new(b, "external_author", json_deep_copy(external));
    assert(message_same_author(a, b));
    const char *fields[] = {"source", "id", "name", "avatar_url"};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        json_object_set_new(json_object_get(b, "external_author"), fields[i], json_string("different"));
        assert(!message_same_author(a, b));
        json_object_set_new(b, "external_author", json_deep_copy(external));
    }
    json_object_set_new(b, "edited_at", json_string("2026-10-15T12:35:56Z"));
    message_blocks blocks;
    assert(message_blocks_build_visual(&blocks,b,"App","",60,true,NULL,NULL,NULL,NULL,NULL,resolve,NULL,NULL,true));
    assert(!blocks.rows[0].header && !strcmp(blocks.rows[0].segments[0].key, "content"));
    assert(blocks.rows[0].segments[0].x == 0);
    assert(!strcmp(blocks.rows[1].segments[0].key, "edited") && blocks.rows[1].segments[0].muted);
    for (size_t i = 0; i < blocks.count; ++i) assert(!blocks.rows[i].header);
    message_blocks_free(&blocks);
    json_object_set_new(b, "attachments", json_pack("[{s:s,s:s,s:s}]", "id", other,
                        "mime_type", "image/png", "original_file_name", "picture.png"));
    json_object_set_new(b, "embeds", json_pack("[{s:s,s:s}]", "id", other, "title", "Preview"));
    assert(message_blocks_build_visual(&blocks,b,"App","",60,true,NULL,NULL,NULL,NULL,NULL,resolve,NULL,NULL,true));
    assert(blocks.media_count == 1);
    bool preview = false;
    for (size_t i = 0; i < blocks.count; ++i) {
        assert(!blocks.rows[i].header);
        for (int j = 0; j < blocks.rows[i].count; ++j)
            preview |= strstr(blocks.rows[i].segments[j].text, "Preview") != NULL;
    }
    assert(preview);
    message_blocks_free(&blocks); json_decref(external); json_decref(a); json_decref(b);
}
int main(void)
{
    assert(setlocale(LC_ALL,"C.UTF-8")); init_graphemebreak();
    sequences(); assets(); time_and_header(); grouping(); font_render(); puts("Emoji, assets, grouping and header checks passed"); return 0;
}
