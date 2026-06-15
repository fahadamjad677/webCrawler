#include "crawler.h"
#include <ctype.h>

/* ─── URL helpers ─────────────────────────────────────────────── */

/* Copy scheme+host from base_url into out (e.g. "https://example.com") */
static void get_origin(const char *base_url, char *out, size_t outlen) {
    /* find end of "https://" prefix */
    const char *sep = strstr(base_url, "://");
    if (!sep) { strncpy(out, base_url, outlen - 1); out[outlen-1]='\0'; return; }
    sep += 3; /* skip "://" */

    /* find next slash after host */
    const char *slash = strchr(sep, '/');
    size_t host_len = slash ? (size_t)(slash - base_url) : strlen(base_url);
    size_t copy = host_len < outlen - 1 ? host_len : outlen - 1;
    memcpy(out, base_url, copy);
    out[copy] = '\0';
}

/* Resolve href relative to base_url, write into out.
 * Handles: absolute http/https, protocol-relative, absolute-path, relative-path */
static void resolve_url(const char *base_url, const char *href,
                        char *out, size_t outlen) {
    /* already absolute */
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        strncpy(out, href, outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }

    char origin[MAX_URL_LEN];
    get_origin(base_url, origin, sizeof(origin));

    if (strncmp(href, "//", 2) == 0) {
        /* protocol-relative: inherit scheme from base */
        const char *scheme_end = strstr(base_url, "://");
        size_t scheme_len = scheme_end ? (size_t)(scheme_end - base_url) : 5;
        char scheme[16] = "https";
        if (scheme_len < sizeof(scheme)) {
            memcpy(scheme, base_url, scheme_len);
            scheme[scheme_len] = '\0';
        }
        snprintf(out, outlen, "%s:%s", scheme, href);
        return;
    }

    if (href[0] == '/') {
        /* absolute path */
        snprintf(out, outlen, "%s%s", origin, href);
        return;
    }

    /* relative path: append to base directory */
    char base_dir[MAX_URL_LEN];
    strncpy(base_dir, base_url, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    char *last_slash = strrchr(base_dir, '/');
    /* only strip if last_slash is after the "://" */
    const char *after_scheme = strstr(base_dir, "://");
    if (last_slash && after_scheme && last_slash > after_scheme + 2)
        *(last_slash + 1) = '\0';
    else
        strncat(base_dir, "/", sizeof(base_dir) - strlen(base_dir) - 1);

    snprintf(out, outlen, "%s%s", base_dir, href);
}

/* Strip fragment (#...) and query string (?...) for deduplication */
static void strip_fragment(char *url) {
    char *p = strchr(url, '#');
    if (p) *p = '\0';
    /* keep query strings — they may point to different content */
}

/* Basic validity check */
static int is_valid_url(const char *url) {
    if (!url || url[0] == '\0') return 0;
    if (strncmp(url, "http://", 7) != 0 &&
        strncmp(url, "https://", 8) != 0) return 0;
    if (strlen(url) >= MAX_URL_LEN) return 0;
    return 1;
}

/* ─── Main extraction ─────────────────────────────────────────── */

/*
 * extract_links: scan `html` for <a href="..."> tags and resolve
 * each to an absolute URL relative to `base_url`.
 *
 * Returns number of links found (up to max_links).
 */
int extract_links(const char *base_url, const char *html, size_t html_len,
                  char links[][MAX_URL_LEN], int max_links) {
    int count = 0;
    if (!html || html_len == 0) return 0;

    const char *p = html;
    const char *end = html + html_len;

    while (p < end && count < max_links) {
        /* find next <a  */
        const char *tag = strcasestr(p, "<a ");
        if (!tag) break;
        p = tag + 3;

        /* find href= within this tag (before >) */
        const char *close = strchr(p, '>');
        if (!close) break;

        const char *href_pos = strcasestr(p, "href=");
        if (!href_pos || href_pos > close) { p = close + 1; continue; }

        href_pos += 5; /* skip "href=" */

        /* handle quoted and unquoted hrefs */
        char quote = *href_pos;
        const char *href_start;
        const char *href_end;

        if (quote == '"' || quote == '\'') {
            href_start = href_pos + 1;
            href_end   = strchr(href_start, quote);
        } else {
            href_start = href_pos;
            href_end   = strpbrk(href_start, " \t\n\r>");
        }

        if (!href_end || href_end <= href_start) { p = close + 1; continue; }

        /* copy href into a local buffer */
        size_t href_len = (size_t)(href_end - href_start);
        if (href_len == 0 || href_len >= MAX_URL_LEN) { p = close + 1; continue; }

        char href[MAX_URL_LEN];
        memcpy(href, href_start, href_len);
        href[href_len] = '\0';

        /* skip javascript: and mailto: */
        if (strncmp(href, "javascript:", 11) == 0 ||
            strncmp(href, "mailto:",     7)  == 0 ||
            strncmp(href, "tel:",        4)  == 0) {
            p = close + 1;
            continue;
        }

        /* resolve to absolute URL */
        char resolved[MAX_URL_LEN];
        resolve_url(base_url, href, resolved, sizeof(resolved));
        strip_fragment(resolved);

        if (is_valid_url(resolved)) {
            strncpy(links[count], resolved, MAX_URL_LEN - 1);
            links[count][MAX_URL_LEN - 1] = '\0';
            count++;
        }

        p = close + 1;
    }

    return count;
}
