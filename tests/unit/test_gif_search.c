#include "net/gif_search.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char response[] =
    "{\"query\":{\"pages\":["
    /* Out of order on purpose: results follow the search ranking ("index"). */
    "{\"title\":\"File:Second_cat\\u202egnp.gif\",\"index\":2,\"imageinfo\":[{"
    "\"url\":\"https://upload.wikimedia.org/wikipedia/commons/a/a1/Second.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/wikipedia/commons/thumb/a/a1/Second.gif/"
    "240px-Second.gif\","
    "\"mime\":\"image/gif\",\"size\":1000,\"width\":300,\"height\":200}]},"
    "{\"title\":\"File:Cat\\u0007_dance.GIF\",\"index\":1,\"imageinfo\":[{"
    "\"url\":\"https://upload.wikimedia.org/wikipedia/commons/b/b2/Cat.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/wikipedia/commons/thumb/b/b2/Cat.gif/"
    "240px-Cat.gif\","
    "\"mime\":\"image/gif\",\"size\":2000,\"width\":480,\"height\":360}]},"
    /* Everything below must be rejected. */
    "{\"title\":\"File:Wrong host.gif\",\"index\":3,\"imageinfo\":[{"
    "\"url\":\"https://evil.example/x.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/t.gif\","
    "\"mime\":\"image/gif\",\"size\":10,\"width\":1,\"height\":1}]},"
    "{\"title\":\"File:Lookalike.gif\",\"index\":4,\"imageinfo\":[{"
    "\"url\":\"https://upload.wikimedia.org.evil.example/x.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/t.gif\","
    "\"mime\":\"image/gif\",\"size\":10,\"width\":1,\"height\":1}]},"
    "{\"title\":\"File:Plain http.gif\",\"index\":5,\"imageinfo\":[{"
    "\"url\":\"http://upload.wikimedia.org/x.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/t.gif\","
    "\"mime\":\"image/gif\",\"size\":10,\"width\":1,\"height\":1}]},"
    "{\"title\":\"File:Not a gif.png\",\"index\":6,\"imageinfo\":[{"
    "\"url\":\"https://upload.wikimedia.org/x.png\","
    "\"thumburl\":\"https://upload.wikimedia.org/t.png\","
    "\"mime\":\"image/png\",\"size\":10,\"width\":1,\"height\":1}]},"
    "{\"title\":\"File:Huge.gif\",\"index\":7,\"imageinfo\":[{"
    "\"url\":\"https://upload.wikimedia.org/h.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/h.gif\","
    "\"mime\":\"image/gif\",\"size\":99999999,\"width\":10,\"height\":10}]},"
    "{\"title\":\"File:Spaces.gif\",\"index\":8,\"imageinfo\":[{"
    "\"url\":\"https://upload.wikimedia.org/a b.gif\","
    "\"thumburl\":\"https://upload.wikimedia.org/t.gif\","
    "\"mime\":\"image/gif\",\"size\":10,\"width\":1,\"height\":1}]},"
    "{\"title\":\"File:No info.gif\",\"index\":9}"
    "]}}";

int main(void)
{
    gif_result *results = NULL;
    size_t count = 0;
    assert(gif_search_parse(response, sizeof(response) - 1, NULL, &results, &count));
    assert(count == 2);
    assert(!strcmp(results[0].title, "Cat dance"));
    assert(!strcmp(results[0].url, "https://upload.wikimedia.org/wikipedia/commons/b/b2/Cat.gif"));
    assert(results[0].width == 480 && results[0].height == 360);
    /* The bidirectional override is replaced, never passed to the terminal. */
    assert(!strstr(results[1].title, "\xe2\x80\xae") &&
           !strncmp(results[1].title, "Second cat", 10));
    gif_results_free(results, count);

    /* A search without matches has no "pages" at all. */
    static const char empty[] = "{\"batchcomplete\":true}";
    assert(gif_search_parse(empty, sizeof(empty) - 1, NULL, &results, &count) && count == 0);
    gif_results_free(results, count);

    /* A local test endpoint only accepts its own origin. */
    static const char local[] =
        "{\"query\":{\"pages\":[{\"title\":\"File:Local.gif\",\"index\":1,\"imageinfo\":[{"
        "\"url\":\"http://127.0.0.1:8080/g/local.gif\","
        "\"thumburl\":\"http://127.0.0.1:8080/g/t.gif\","
        "\"mime\":\"image/gif\",\"size\":10,\"width\":4,\"height\":4}]}]}}";
    assert(gif_search_parse(local, sizeof(local) - 1, "http://127.0.0.1:8080", &results, &count) &&
           count == 1);
    gif_results_free(results, count);
    assert(gif_search_parse(local, sizeof(local) - 1, "http://127.0.0.1:9", &results, &count) &&
           count == 0);
    gif_results_free(results, count);
    assert(gif_search_parse(local, sizeof(local) - 1, NULL, &results, &count) && count == 0);
    gif_results_free(results, count);

    for (const char *bad[] = {"", "[]", "not json", "null"}, **p = bad; p != bad + 4; ++p) {
        assert(!gif_search_parse(*p, strlen(*p), NULL, &results, &count) && !results && !count);
    }
    puts("GIF search results are ranked, sanitized and limited to trusted addresses");
    return 0;
}
