#include "net/signature.h"
#include <glib.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ARMOR_BEGIN "-----BEGIN PGP SIGNATURE-----"
#define ARMOR_END "-----END PGP SIGNATURE-----"
/* RFC 4880: packet tag 2 is a signature; these are the numbers it uses. */
#define TAG_SIGNATURE 2
#define VERSION_4 4
#define CLASS_BINARY 0x00
#define ALGORITHM_RSA 1
#define HASH_SHA256 8
#define HASH_SHA512 10
/* An RSA signature of a key of up to 8192 bits. */
#define MPI_LIMIT 1024U

/* The next line of text, without its line ending; false at the end. */
static bool next_line(const char **cursor, const char *end, const char **line, size_t *length)
{
    if (*cursor >= end)
        return false;
    const char *start = *cursor, *stop = memchr(start, '\n', (size_t)(end - start));
    *cursor = stop ? stop + 1 : end;
    if (!stop)
        stop = end;
    while (stop > start && (stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t'))
        --stop;
    *line = start;
    *length = (size_t)(stop - start);
    return true;
}
static bool is_line(const char *line, size_t length, const char *text)
{
    return length == strlen(text) && !memcmp(line, text, length);
}
/* The bytes inside the armor: the Radix-64 lines between the headers, which
 * end at a blank line, and the checksum line or the end marker. */
static unsigned char *dearmor(const char *armored, size_t armored_length, size_t *size)
{
    const char *cursor = armored, *end = armored + armored_length, *line;
    size_t length;
    bool begun = false, headers = true, ended = false;
    GString *radix = g_string_new(NULL);
    while (!ended && next_line(&cursor, end, &line, &length)) {
        if (!begun) {
            begun = is_line(line, length, ARMOR_BEGIN);
            continue;
        }
        if (is_line(line, length, ARMOR_END))
            ended = true;
        else if (headers)
            headers = length != 0;
        else if (length && line[0] == '=')
            continue; /* The CRC-24 of the armor; the signature is what is checked. */
        else
            g_string_append_len(radix, line, (gssize)length);
    }
    unsigned char *bytes = NULL;
    *size = 0;
    if (ended && !headers && radix->len) {
        gsize decoded = 0;
        bytes = g_base64_decode(radix->str, &decoded);
        *size = decoded;
        /* Radix-64 decodes three bytes from four characters: a text that is
         * not Radix-64 gives another count. */
        if (!decoded || decoded != radix->len / 4 * 3 - (g_str_has_suffix(radix->str, "==")  ? 2
                                                         : g_str_has_suffix(radix->str, "=") ? 1
                                                                                             : 0)) {
            g_free(bytes);
            bytes = NULL;
            *size = 0;
        }
    }
    g_string_free(radix, TRUE);
    return bytes;
}
/* The body of the one packet the bytes hold; false unless it is exactly one
 * packet of a known length. */
static bool one_packet(const unsigned char *bytes, size_t size, unsigned *tag,
                       const unsigned char **body, size_t *length)
{
    if (size < 2 || !(bytes[0] & 0x80))
        return false;
    size_t header;
    if (bytes[0] & 0x40) { /* New format. */
        *tag = bytes[0] & 0x3f;
        if (bytes[1] < 192) {
            *length = bytes[1];
            header = 2;
        } else if (bytes[1] < 224 && size >= 3) {
            *length = (((size_t)bytes[1] - 192) << 8) + bytes[2] + 192;
            header = 3;
        } else
            return false; /* Partial and four-byte lengths do not occur here. */
    } else {
        *tag = (bytes[0] >> 2) & 0x0f;
        unsigned type = bytes[0] & 0x03;
        if (type == 0) {
            *length = bytes[1];
            header = 2;
        } else if (type == 1 && size >= 3) {
            *length = ((size_t)bytes[1] << 8) | bytes[2];
            header = 3;
        } else
            return false;
    }
    *body = bytes + header;
    return *length == size - header;
}
signature_result signature_verify(const unsigned char *data, size_t length, const char *armored,
                                  size_t armored_length, const unsigned char *key,
                                  size_t key_length)
{
    if (!data || !armored || !key || !armored_length || armored_length > SIGNATURE_ARMOR_LIMIT)
        return SIGNATURE_MALFORMED;
    size_t size, packet;
    unsigned char *bytes = dearmor(armored, armored_length, &size);
    const unsigned char *body = NULL;
    unsigned tag = 0;
    signature_result result = SIGNATURE_MALFORMED;
    EVP_PKEY *signer = NULL;
    EVP_MD_CTX *context = NULL;
    unsigned char padded[MPI_LIMIT] = {0};
    if (!bytes || !one_packet(bytes, size, &tag, &body, &packet))
        goto done;
    result = SIGNATURE_UNSUPPORTED;
    /* version, class, key algorithm, hash algorithm, hashed length (2). */
    if (tag != TAG_SIGNATURE || packet < 6 || body[0] != VERSION_4 || body[1] != CLASS_BINARY ||
        body[2] != ALGORITHM_RSA || (body[3] != HASH_SHA256 && body[3] != HASH_SHA512))
        goto done;
    result = SIGNATURE_MALFORMED;
    size_t hashed = 6 + (((size_t)body[4] << 8) | body[5]);
    if (hashed + 2 > packet)
        goto done;
    size_t unhashed = ((size_t)body[hashed] << 8) | body[hashed + 1];
    /* After the unhashed part: two bytes of the digest, then the signature
     * as one number with its length in bits. */
    size_t number = hashed + 2 + unhashed + 2;
    if (number + 2 > packet)
        goto done;
    size_t bits = ((size_t)body[number] << 8) | body[number + 1];
    size_t octets = (bits + 7) / 8;
    if (!octets || octets > MPI_LIMIT || number + 2 + octets != packet)
        goto done;
    const unsigned char *pointer = key;
    signer = d2i_PUBKEY(NULL, &pointer, (long)key_length);
    context = EVP_MD_CTX_new();
    int modulus = signer ? EVP_PKEY_size(signer) : 0;
    if (!signer || !context || EVP_PKEY_base_id(signer) != EVP_PKEY_RSA || modulus <= 0 ||
        (size_t)modulus > MPI_LIMIT || octets > (size_t)modulus)
        goto done;
    /* The number drops its leading zero bytes; RSA wants the key's full width. */
    memcpy(padded + (size_t)modulus - octets, body + number + 2, octets);
    /* What a version 4 signature covers: the document, the packet up to the
     * end of its hashed part, and a trailer with that part's length. */
    const unsigned char trailer[6] = {VERSION_4,
                                      0xff,
                                      (unsigned char)(hashed >> 24),
                                      (unsigned char)(hashed >> 16),
                                      (unsigned char)(hashed >> 8),
                                      (unsigned char)hashed};
    result = SIGNATURE_WRONG;
    if (EVP_DigestVerifyInit(context, NULL, body[3] == HASH_SHA512 ? EVP_sha512() : EVP_sha256(),
                             NULL, signer) == 1 &&
        EVP_DigestVerifyUpdate(context, data, length) == 1 &&
        EVP_DigestVerifyUpdate(context, body, hashed) == 1 &&
        EVP_DigestVerifyUpdate(context, trailer, sizeof(trailer)) == 1 &&
        EVP_DigestVerifyFinal(context, padded, (size_t)modulus) == 1)
        result = SIGNATURE_VALID;
done:
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(signer);
    g_free(bytes);
    return result;
}
