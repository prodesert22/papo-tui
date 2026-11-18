#ifndef PAPO_HTTP_EASY_H
#define PAPO_HTTP_EASY_H
#include "net/tls.h"
#include <curl/curl.h>

/* The policy shared by every transfer outside the REST client: redirects are
 * never followed by libcurl, TLS is verified against the configured CA bundle
 * and the program identifies itself. The caller owns the handle and adds the
 * allowed protocols, the body limit and the transfer timeouts. */
static inline CURL *net_easy_base(const char *url)
{
    CURL *easy = curl_easy_init();
    if (!easy)
        return NULL;
    const char *ca_bundle = net_ca_bundle();
    if (curl_easy_setopt(easy, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_USERAGENT, "papo-tui/" PAPO_VERSION) != CURLE_OK ||
        (ca_bundle && *ca_bundle &&
         curl_easy_setopt(easy, CURLOPT_CAINFO, ca_bundle) != CURLE_OK)) {
        curl_easy_cleanup(easy);
        return NULL;
    }
    return easy;
}

#endif
