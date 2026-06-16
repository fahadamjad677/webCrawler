#include "filter.h"
#include <ctype.h>
#include <errno.h>

/* ─── Internal helpers ──────────────────────────────────────────── */

/* Case-insensitive substring search */
static int url_contains(const char *url, const char *keyword) {
    if (!keyword || keyword[0] == '\0') return 1;
    size_t klen = strlen(keyword);
    size_t ulen = strlen(url);
    if (klen > ulen) return 0;
    for (size_t i = 0; i <= ulen - klen; i++) {
        size_t j;
        for (j = 0; j < klen; j++) {
            if (tolower((unsigned char)url[i+j]) !=
                tolower((unsigned char)keyword[j])) break;
        }
        if (j == klen) return 1;
    }
    return 0;
}

/* Exact match or prefix match if pattern ends with '*' */
static int url_matches_pattern(const char *url, const char *pattern) {
    size_t plen = strlen(pattern);
    if (plen > 0 && pattern[plen-1] == '*')
        return strncasecmp(url, pattern, plen-1) == 0;
    return strcasecmp(url, pattern) == 0;
}

/* Strip leading/trailing whitespace in-place */
static void trim(char *s) {
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s+i, strlen(s)-i+1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

/* ─── Public API ────────────────────────────────────────────────── */

int filter_matches(const CrawlFilter *f, const char *url) {
    switch (f->type) {

    case FILTER_ALL:
        return 1;

    case FILTER_KEYWORD:
        return url_contains(url, f->pattern);

    case FILTER_URL:
        return url_matches_pattern(url, f->pattern);

    case FILTER_FILE:
        for (int i = 0; i < f->pattern_count; i++) {
            const char *p = f->patterns[i];
            /* lines prefixed "url:" → exact/prefix match */
            if (strncasecmp(p, "url:", 4) == 0) {
                if (url_matches_pattern(url, p + 4)) return 1;
            } else {
                /* plain line → keyword match */
                if (url_contains(url, p)) return 1;
            }
        }
        return 0;
    }
    return 1; /* unreachable */
}

int filter_load_file(CrawlFilter *f, const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "[FILTER] Cannot open file: %s (%s)\n",
                filepath, strerror(errno));
        return -1;
    }

    f->pattern_count = 0;
    char line[MAX_URL_LEN];

    while (fgets(line, sizeof(line), fp) &&
           f->pattern_count < MAX_FILE_PATTERNS) {
        /* strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]='\0';
        trim(line);
        if (len == 0 || line[0] == '#') continue;  /* skip blank/comments */

        strncpy(f->patterns[f->pattern_count], line, MAX_URL_LEN-1);
        f->patterns[f->pattern_count][MAX_URL_LEN-1] = '\0';
        f->pattern_count++;
    }
    fclose(fp);
    return f->pattern_count;
}

void filter_describe(const CrawlFilter *f, char *buf, size_t bufsz) {
    switch (f->type) {
    case FILTER_ALL:
        snprintf(buf, bufsz, "all URLs (no filter)");
        break;
    case FILTER_KEYWORD:
        snprintf(buf, bufsz, "keyword \"%s\"", f->pattern);
        break;
    case FILTER_URL:
        snprintf(buf, bufsz, "URL pattern \"%s\"", f->pattern);
        break;
    case FILTER_FILE:
        snprintf(buf, bufsz, "file \"%s\" (%d patterns)",
                 f->pattern, f->pattern_count);
        break;
    }
}