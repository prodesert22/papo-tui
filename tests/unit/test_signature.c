#include "net/signature.h"
#include "net/update.h"
#include "net/update_key.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static signature_result verify(const char *data, gsize length, const char *armored)
{
    return signature_verify((const unsigned char *)data, length, armored, strlen(armored),
                            update_release_key, sizeof(update_release_key));
}

int main(int argc, char **argv)
{
    char *manifest = NULL, *armored = NULL, *other = NULL;
    gsize length = 0, armored_length = 0, other_length = 0;
    CHECK(argc == 4);
    CHECK(g_file_get_contents(argv[1], &manifest, &length, NULL));
    CHECK(g_file_get_contents(argv[2], &armored, &armored_length, NULL));
    CHECK(g_file_get_contents(argv[3], &other, &other_length, NULL));

    /* The REAL manifest of the 1.0.0-beta.2 release and the signature gpg made
     * for it verify with the key built into the program. */
    CHECK(verify(manifest, length, armored) == SIGNATURE_VALID);

    /* One byte of the manifest, or one fewer, and it is not that signature. */
    char *changed = g_memdup2(manifest, length + 1);
    changed[100] ^= 1;
    CHECK(verify(changed, length, armored) == SIGNATURE_WRONG);
    CHECK(verify(manifest, length - 1, armored) == SIGNATURE_WRONG);
    CHECK(verify(manifest, 0, armored) == SIGNATURE_WRONG);
    g_free(changed);

    /* A signature changed inside its number is well formed and wrong. */
    char *middle = strchr(armored, '\n');
    CHECK(middle && (middle = strstr(middle, "\n\n")));
    size_t offset = (size_t)(middle - armored) + 2 + 400;
    CHECK(offset < armored_length);
    char *forged = g_strdup(armored);
    forged[offset] = forged[offset] == 'A' ? 'B' : 'A';
    CHECK(verify(manifest, length, forged) == SIGNATURE_WRONG);
    g_free(forged);

    /* Another key does not verify it: here the key is one of its own bytes off. */
    unsigned char stranger[sizeof(update_release_key)];
    memcpy(stranger, update_release_key, sizeof(stranger));
    stranger[200] ^= 1;
    CHECK(signature_verify((const unsigned char *)manifest, length, armored, armored_length,
                           stranger, sizeof(stranger)) != SIGNATURE_VALID);
    CHECK(signature_verify((const unsigned char *)manifest, length, armored, armored_length,
                           (const unsigned char *)"not a key", 9) != SIGNATURE_VALID);

    /* What is not one armored signature packet is refused as malformed. */
    char *no_end = g_strndup(armored, (gsize)(strstr(armored, "-----END") - armored));
    char *no_blank = g_strdup(armored);
    char *blank = strstr(no_blank, "\n\n");
    CHECK(blank);
    memmove(blank + 1, blank + 2, strlen(blank + 2) + 1);
    char *truncated = g_strdup(armored);
    char *data_line = strstr(truncated, "\n\n") + 2;
    memmove(data_line, data_line + 8, strlen(data_line + 8) + 1);
    char *twice = g_strconcat(armored, armored, NULL);
    const char *const malformed[] = {
        "",
        "not a signature",
        "-----BEGIN PGP SIGNATURE-----\n\n-----END PGP SIGNATURE-----\n",
        "-----BEGIN PGP SIGNATURE-----\n\n!!!!\n-----END PGP SIGNATURE-----\n",
        "-----BEGIN PGP SIGNATURE-----\n\nAAAA\n-----END PGP SIGNATURE-----\n",
        "-----BEGIN PGP MESSAGE-----\n\niQAA\n-----END PGP MESSAGE-----\n",
        no_end,
        no_blank,
        truncated,
    };
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        if (verify(manifest, length, malformed[i]) != SIGNATURE_MALFORMED) {
            fprintf(stderr, "not refused as malformed: %zu\n", i);
            return 1;
        }
    }
    /* Only the first armored block is read, and it is the valid one here. */
    CHECK(verify(manifest, length, twice) == SIGNATURE_VALID);
    g_free(twice);
    g_free(truncated);
    g_free(no_blank);
    g_free(no_end);

    /* Signatures of other kinds are refused before any arithmetic. The packet
     * below is the real one with one field changed each time. */
    gsize packet_length = 0;
    char *radix = g_strndup(middle + 2, (gsize)(strstr(middle, "\n=") - (middle + 2)));
    unsigned char *packet = g_base64_decode(radix, &packet_length);
    CHECK(packet && packet_length > 16 && packet[3] == 4);
    static const struct {
        size_t at;
        unsigned char value;
    } other_kinds[] = {
        {3, 3},  /* version 3 */
        {3, 5},  /* version 5 */
        {4, 1},  /* a text document */
        {4, 16}, /* a certification of a key */
        {5, 22}, /* EdDSA */
        {5, 17}, /* DSA */
        {6, 2},  /* SHA-1 */
        {6, 1},  /* MD5 */
    };
    for (size_t i = 0; i < sizeof(other_kinds) / sizeof(other_kinds[0]); ++i) {
        unsigned char *copy = g_memdup2(packet, packet_length);
        copy[other_kinds[i].at] = other_kinds[i].value;
        char *encoded = g_base64_encode(copy, packet_length);
        char *block = g_strconcat("-----BEGIN PGP SIGNATURE-----\n\n", encoded,
                                  "\n-----END PGP SIGNATURE-----\n", NULL);
        if (verify(manifest, length, block) != SIGNATURE_UNSUPPORTED) {
            fprintf(stderr, "another kind of signature was read: %zu\n", i);
            return 1;
        }
        g_free(block);
        g_free(encoded);
        g_free(copy);
    }
    /* A length inside the packet that reaches past it is malformed. */
    unsigned char *reach = g_memdup2(packet, packet_length);
    reach[7] = 0xff;
    char *encoded = g_base64_encode(reach, packet_length);
    char *block = g_strconcat("-----BEGIN PGP SIGNATURE-----\n\n", encoded,
                              "\n-----END PGP SIGNATURE-----\n", NULL);
    CHECK(verify(manifest, length, block) == SIGNATURE_MALFORMED);
    g_free(block);
    g_free(encoded);
    g_free(reach);
    g_free(packet);
    g_free(radix);
    char huge[SIGNATURE_ARMOR_LIMIT + 2];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = 0;
    CHECK(verify(manifest, length, huge) == SIGNATURE_MALFORMED);
    CHECK(signature_verify(NULL, 0, armored, armored_length, update_release_key,
                           sizeof(update_release_key)) == SIGNATURE_MALFORMED);

    /* The signed manifest names the sum of each file of the release; the real
     * one agrees with the digest GitHub lists for the same file. */
    char sum[UPDATE_HASH_BYTES];
    CHECK(
        update_manifest_hash(manifest, length, "PapoTUI-1.0.0-beta.2-linux-x86_64.AppImage", sum));
    CHECK(!strcmp(sum, "6ed4a373fb1eb1c529b0e87d7caefbbf66f0ba13f31e7235f3fee87fa5ab1182"));
    CHECK(strstr(other, sum));
    CHECK(update_manifest_hash(manifest, length, "papo-tui-1.0.0-beta.2-termux-aarch64.deb", sum));
    /* A name that is only the start, the end or a part of a listed one is not listed. */
    CHECK(!update_manifest_hash(manifest, length, "PapoTUI-1.0.0-beta.2-linux-x86_64", sum));
    CHECK(!update_manifest_hash(manifest, length, "linux-x86_64.AppImage", sum));
    CHECK(!update_manifest_hash(manifest, length, "absent.deb", sum));
    CHECK(!update_manifest_hash(manifest, length, "", sum));
    CHECK(!update_manifest_hash(manifest, length, NULL, sum));
#define SUM_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SUM_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    const char *listed = SUM_A "  one.deb\r\n" SUM_B " *two.zip\n" SUM_A "  three.rpm";
    CHECK(update_manifest_hash(listed, strlen(listed), "one.deb", sum) && !strcmp(sum, SUM_A));
    CHECK(update_manifest_hash(listed, strlen(listed), "two.zip", sum) && !strcmp(sum, SUM_B));
    CHECK(update_manifest_hash(listed, strlen(listed), "three.rpm", sum) && !strcmp(sum, SUM_A));
    const char *twice_same = SUM_A "  one.deb\n" SUM_A "  one.deb\n";
    CHECK(update_manifest_hash(twice_same, strlen(twice_same), "one.deb", sum));
    const char *twice_other = SUM_A "  one.deb\n" SUM_B "  one.deb\n";
    CHECK(!update_manifest_hash(twice_other, strlen(twice_other), "one.deb", sum));
    const char *upper =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA  one.deb\n";
    CHECK(!update_manifest_hash(upper, strlen(upper), "one.deb", sum));
    const char *one_space = SUM_A " one.deb\n";
    CHECK(!update_manifest_hash(one_space, strlen(one_space), "one.deb", sum));
    const char with_nul[] = SUM_A "  one.deb\n\0" SUM_B "  two.zip\n";
    CHECK(!update_manifest_hash(with_nul, sizeof(with_nul) - 1, "one.deb", sum));

    g_free(other);
    g_free(armored);
    g_free(manifest);
    return 0;
}
