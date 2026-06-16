#ifndef FILTER_H
#define FILTER_H

#include "crawler.h"

/* ─── Filter types ──────────────────────────────────────────────── */
typedef enum {
    FILTER_ALL,      /* crawl everything (no filter)     */
    FILTER_KEYWORD,  /* URL must contain keyword          */
    FILTER_URL,      /* URL must match exact/prefix       */
    FILTER_FILE      /* URL must match any line in file   */
} FilterType;

/* ─── Filter state ──────────────────────────────────────────────── */
#define MAX_FILE_PATTERNS  256   /* max lines loaded from filter file */

typedef struct CrawlFilter {
    FilterType type;
    char       pattern[MAX_URL_LEN];               /* keyword or URL pattern  */
    char       patterns[MAX_FILE_PATTERNS][MAX_URL_LEN]; /* loaded from file  */
    int        pattern_count;                       /* how many loaded         */
} CrawlFilter;

/* ─── API ───────────────────────────────────────────────────────── */

/*
 * filter_matches: returns 1 if url should be crawled, 0 if it should
 * be skipped.  FILTER_ALL always returns 1.
 */
int  filter_matches(const CrawlFilter *f, const char *url);

/*
 * filter_load_file: populate f->patterns[] from a file (one pattern
 * per line).  Lines starting with '#' are skipped.
 * Returns number of patterns loaded, -1 on error.
 */
int  filter_load_file(CrawlFilter *f, const char *filepath);

/* filter_describe: write a short human-readable description into buf */
void filter_describe(const CrawlFilter *f, char *buf, size_t bufsz);

#endif /* FILTER_H */