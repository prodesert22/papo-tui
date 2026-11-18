#ifndef PAPO_NET_SIGNATURE_H
#define PAPO_NET_SIGNATURE_H

#include <stddef.h>

/* Detached OpenPGP signatures, as "gpg --armor --detach-sign" makes them for a
 * release's manifest, checked against one RSA key. Only that form is read: an
 * armored block with one version 4 signature packet over a binary document,
 * made with RSA and SHA-256 or SHA-512. Anything else is refused, never
 * guessed at. The arithmetic is OpenSSL's; this module only reads the packet
 * and hashes what OpenPGP says the signature covers. */
typedef enum {
    SIGNATURE_VALID,
    SIGNATURE_MALFORMED,   /* Not an armored block holding one signature packet. */
    SIGNATURE_UNSUPPORTED, /* A signature of another kind, version or algorithm. */
    SIGNATURE_WRONG        /* Well formed, but not this key's signature of this data. */
} signature_result;

#define SIGNATURE_ARMOR_LIMIT (64U * 1024U)

/* data is the signed document, byte for byte. armored is the signature file's
 * text; key is the signer's public key as a DER SubjectPublicKeyInfo. All are
 * borrowed. */
signature_result signature_verify(const unsigned char *data, size_t length, const char *armored,
                                  size_t armored_length, const unsigned char *key,
                                  size_t key_length);

#endif
