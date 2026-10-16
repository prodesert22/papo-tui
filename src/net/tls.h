#ifndef PAPO_TLS_H
#define PAPO_TLS_H
#include <glib.h>
#include <stdlib.h>

/* A portable build still trusts the destination system's CA store. An explicit
 * override retains priority for private instances and integration fixtures. */
static inline const char *net_ca_bundle(void)
{
    const char *override = getenv("CURL_CA_BUNDLE");
    if (override && *override)
        return override;
#ifndef _WIN32
    static const char *const paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(paths); ++i)
        if (g_file_test(paths[i], G_FILE_TEST_IS_REGULAR))
            return paths[i];
#endif
    return NULL; /* Schannel uses the Windows certificate store. */
}
#endif
