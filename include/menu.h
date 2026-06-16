#ifndef MENU_H
#define MENU_H

#include "crawler.h"

/* ─── Search result ─────────────────────────────────────────────── */

#define MAX_RESULTS     500     /* cap on results returned per search */

typedef enum {
    SRC_VISITED,    /* match came from visited.txt  */
    SRC_QUEUE,      /* match came from queue.txt    */
    SRC_LOG,        /* match came from crawler.log  */
    SRC_FILE        /* match came from a user file  */
} ResultSource;

typedef struct {
    char         url[MAX_URL_LEN];
    ResultSource source;
    int          line_no;   /* 1-based line in the file    */
} SearchResult;

typedef struct {
    SearchResult items[MAX_RESULTS];
    int          count;
    int          truncated;  /* 1 if more results exist but were cut  */
} SearchResultSet;

/* ─── Search modes ──────────────────────────────────────────────── */

/*
 * search_by_keyword  – substring match on URL strings
 * search_by_url      – exact-URL lookup (or prefix match with wildcard *)
 * search_by_file     – read a file of URLs/keywords, run each query
 *
 * All three functions read from the data_dir files directly
 * (visited.txt, queue.txt) — the crawler lock must NOT be held
 * by the caller when these are called interactively from the menu.
 */
void search_by_keyword(const char *data_dir, const char *keyword,
                       SearchResultSet *out);

void search_by_url    (const char *data_dir, const char *target_url,
                       SearchResultSet *out);

void search_by_file   (const char *data_dir, const char *file_path,
                       SearchResultSet *out);   /* file has one query per line */

/* ─── Result display ─────────────────────────────────────────────── */

void print_results(const SearchResultSet *r, const char *query);

/* ─── Menu entry point ───────────────────────────────────────────── */

/*
 * run_menu: interactive loop shown after (or instead of) the crawl.
 * Reads data_dir so it can work on completed crawl data even when
 * called with no active CrawlerState (cs may be NULL for offline mode).
 */
void run_menu(CrawlerState *cs, const char *data_dir);

#endif /* MENU_H */