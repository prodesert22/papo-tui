#include "model/conversation/server_admin.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ME "00000000-0000-4000-8000-000000000001"
#define OWNER "00000000-0000-4000-8000-000000000002"
#define ROLE "00000000-0000-4000-8000-000000000003"

static permissions *account(const char *owner, const char *flag)
{
    permissions *p = permissions_create();
    json_t *whoami = json_pack("{s:s,s:[{s:s}]}", "id", ME, "roles", "id", ROLE);
    json_t *roles = json_pack("{s:[{s:s,s:s,s:{s:b}}]}", "roles", "id", ROLE, "name", "Moderação",
                              "permissions", flag ? flag : "pin_message", flag != NULL);
    json_t *server = json_pack("{s:s}", "owner_id", owner);
    bool ok = p && whoami && roles && server && permissions_identity(p, whoami, 0) &&
              permissions_update(p, PERMISSIONS_ROLES, roles) &&
              permissions_update(p, PERMISSIONS_SERVER, server);
    json_decref(whoami);
    json_decref(roles);
    json_decref(server);
    if (!ok) {
        permissions_destroy(p);
        return NULL;
    }
    return p;
}

static int visibility(void)
{
    permissions *owner = account(ME, NULL);
    CHECK(owner);
    for (int tab = 0; tab < ADMIN_TABS; ++tab)
        CHECK(server_admin_visibility(owner, (admin_tab)tab) == ADMIN_SHOWN);
    permissions_destroy(owner);

    permissions *member = account(OWNER, NULL);
    CHECK(member);
    CHECK(server_admin_visibility(member, ADMIN_TAB_EMOJIS) == ADMIN_SHOWN);
    CHECK(server_admin_visibility(member, ADMIN_TAB_OVERVIEW) == ADMIN_HIDDEN);
    CHECK(server_admin_visibility(member, ADMIN_TAB_ROLES) == ADMIN_HIDDEN);
    CHECK(server_admin_visibility(member, ADMIN_TAB_CHANNELS) == ADMIN_HIDDEN);
    CHECK(server_admin_visibility(member, ADMIN_TAB_MEMBERS) == ADMIN_HIDDEN);
    CHECK(server_admin_visibility(member, ADMIN_TAB_AUDIT) == ADMIN_HIDDEN);
    permissions_destroy(member);

    permissions *roles = account(OWNER, "manage_roles");
    CHECK(roles);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_ROLES) == ADMIN_SHOWN);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_MEMBERS) == ADMIN_SHOWN);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_CHANNELS) == ADMIN_HIDDEN);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_AUDIT) == ADMIN_HIDDEN);
    /* Without the owner confirmed, a missing permission is pending, never shown. */
    permissions_invalidate(roles, PERMISSIONS_SERVER);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_OVERVIEW) == ADMIN_PENDING);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_ROLES) == ADMIN_SHOWN);
    CHECK(server_admin_visibility(roles, ADMIN_TAB_EMOJIS) == ADMIN_SHOWN);
    permissions_destroy(roles);

    permissions *server = account(OWNER, "manage_server");
    CHECK(server);
    CHECK(server_admin_visibility(server, ADMIN_TAB_OVERVIEW) == ADMIN_SHOWN);
    CHECK(server_admin_visibility(server, ADMIN_TAB_AUDIT) == ADMIN_SHOWN);
    /* Banning and reset links need manage_server, so Membros shows too. */
    CHECK(server_admin_visibility(server, ADMIN_TAB_MEMBERS) == ADMIN_SHOWN);
    CHECK(server_admin_visibility(server, ADMIN_TAB_ROLES) == ADMIN_HIDDEN);
    permissions_destroy(server);

    permissions *unknown = permissions_create();
    CHECK(server_admin_visibility(unknown, ADMIN_TAB_CHANNELS) == ADMIN_PENDING);
    CHECK(server_admin_visibility(unknown, ADMIN_TAB_MEMBERS) == ADMIN_PENDING);
    permissions_destroy(unknown);
    return 0;
}

static int validation(void)
{
    CHECK(server_admin_check_text("", 0, true));
    CHECK(server_admin_check_text("   ", 0, true));
    CHECK(!server_admin_check_text("", 0, false));
    CHECK(!server_admin_check_text("Comunidade", ADMIN_SERVER_NAME_MAX, true));
    /* 32 characters fit, whatever their byte length. */
    CHECK(
        !server_admin_check_text("áéíóúáéíóúáéíóúáéíóúáéíóúáéíóúáé", ADMIN_SERVER_NAME_MAX, true));
    CHECK(
        server_admin_check_text("123456789012345678901234567890123", ADMIN_SERVER_NAME_MAX, true));
    CHECK(server_admin_check_text("duas\nlinhas", 0, true));
    CHECK(server_admin_check_text("escape\x1b]52;c;x\x07", 0, true));
    CHECK(server_admin_check_text("\xff", 0, true));
    CHECK(server_admin_color_valid(""));
    CHECK(server_admin_color_valid("#3BA55d"));
    CHECK(!server_admin_color_valid("3BA55D"));
    CHECK(!server_admin_color_valid("#3BA55"));
    CHECK(!server_admin_color_valid("#GGGGGG"));
    CHECK(!server_admin_color_valid(NULL));
    return 0;
}

static int patch(void)
{
    const char *problem;
    json_t *public_server = json_pack("{s:s,s:b}", "name", "Comunidade", "public", 1);
    json_t *private_server = json_pack("{s:s,s:b}", "name", "Comunidade", "public", 0);
    CHECK(public_server && private_server);

    CHECK(!server_admin_patch_body(NULL, "x", -1, NULL, &problem) && problem);
    CHECK(!server_admin_patch_body(public_server, "Comunidade", 1, NULL, &problem) && problem &&
          strstr(problem, "Nenhuma"));
    json_t *body = server_admin_patch_body(public_server, "Clube", 1, NULL, &problem);
    CHECK(body && !problem && json_object_size(body) == 1 &&
          !strcmp(json_string_value(json_object_get(body, "name")), "Clube"));
    json_decref(body);
    CHECK(!server_admin_patch_body(public_server, "", 1, NULL, &problem) && problem);

    /* Making a public server private needs its new password. */
    CHECK(!server_admin_patch_body(public_server, "Comunidade", 0, "", &problem) && problem &&
          strstr(problem, "senha"));
    body = server_admin_patch_body(public_server, "Comunidade", 0, "Segredo!42", &problem);
    CHECK(body && json_object_size(body) == 2 && json_is_false(json_object_get(body, "public")) &&
          !strcmp(json_string_value(json_object_get(body, "password")), "Segredo!42"));
    json_decref(body);

    /* A private server keeps its password unless a new one is given. */
    CHECK(!server_admin_patch_body(private_server, "Comunidade", 0, "", &problem));
    body = server_admin_patch_body(private_server, "Comunidade", 0, "Outra!42", &problem);
    CHECK(body && json_object_size(body) == 1 && json_object_get(body, "password"));
    json_decref(body);

    /* Opening the server never sends a password. */
    body = server_admin_patch_body(private_server, "Comunidade", 1, "ignorada", &problem);
    CHECK(body && json_object_size(body) == 1 && json_is_true(json_object_get(body, "public")));
    json_decref(body);

    json_decref(public_server);
    json_decref(private_server);
    return 0;
}

static int audit(void)
{
    admin_audit_query q;
    memset(&q, 0, sizeof(q));
    CHECK(server_admin_audit_valid(&q));
    strcpy(q.entity, "user_role");
    strcpy(q.action, "role.create");
    strcpy(q.actor, ME);
    strcpy(q.until, "2026-10-11T23:59:59Z");
    q.from = UINT64_C(1760140800000); /* 2025-10-11T00:00:00Z */
    CHECK(server_admin_audit_valid(&q));
    admin_audit_query bad = q;
    strcpy(bad.entity, "planet");
    CHECK(!server_admin_audit_valid(&bad));
    bad = q;
    strcpy(bad.actor, "not-a-uuid");
    CHECK(!server_admin_audit_valid(&bad));
    bad = q;
    strcpy(bad.action, "role create");
    CHECK(!server_admin_audit_valid(&bad));
    bad = q;
    strcpy(bad.action, "role.\x1b[2J");
    CHECK(!server_admin_audit_valid(&bad));
    bad = q;
    strcpy(bad.until, "amanhã");
    CHECK(!server_admin_audit_valid(&bad));
    /* The start may not be after the end. */
    bad = q;
    bad.from = UINT64_C(1893456000000); /* 2030 */
    CHECK(!server_admin_audit_valid(&bad));
    CHECK(!server_admin_audit_valid(NULL));
    return 0;
}

int main(void)
{
    if (visibility() || validation() || patch() || audit())
        return 1;
    puts("server administration rules passed");
    return 0;
}
