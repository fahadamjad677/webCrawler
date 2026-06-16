#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ─── Internal helpers ──────────────────────────────────────────── */

/* Case-insensitive substring search (like strcasestr, but portable) */
static int url_contains(const char *url, const char *keyword) {
    if (!keyword || keyword[0] == '\0') return 1;   /* empty = match all */

    size_t klen = strlen(keyword);
    size_t ulen = strlen(url);
    if (klen > ulen) return 0;

    for (size_t i = 0; i <= ulen - klen; i++) {
        size_t j;
        for (j = 0; j < klen; j++) {
            if (tolower((unsigned char)url[i + j]) !=
                tolower((unsigned char)keyword[j])) break;
        }
        if (j == klen) return 1;
    }
    return 0;
}

/*
 * Exact match OR prefix-wildcard:
 *   "https://example.com/foo"   -> exact
 *   "https://example.com/foo*"  -> prefix (strip trailing *)
 */
static int url_matches(const char *url, const char *target) {
    size_t tlen = strlen(target);
    if (tlen > 0 && target[tlen - 1] == '*') {
        /* prefix match */
        return strncasecmp(url, target, tlen - 1) == 0;
    }
    return strcasecmp(url, target) == 0;
}

/* Append one result to the set (respects MAX_RESULTS cap) */
static void append_result(SearchResultSet *out,
                           const char *url, ResultSource src, int lineno) {
    if (out->count >= MAX_RESULTS) {
        out->truncated = 1;
        return;
    }
    strncpy(out->items[out->count].url, url, MAX_URL_LEN - 1);
    out->items[out->count].url[MAX_URL_LEN - 1] = '\0';
    out->items[out->count].source  = src;
    out->items[out->count].line_no = lineno;
    out->count++;
}

/* Scan one file (visited.txt or queue.txt) for a predicate */
typedef int (*MatchFn)(const char *url, const char *pattern);

static void scan_file(const char *filepath, ResultSource src,
                      MatchFn match, const char *pattern,
                      SearchResultSet *out) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;   /* file may not exist yet — silently skip */

    char line[MAX_URL_LEN];
    int  lineno = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        /* strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        if (match(line, pattern))
            append_result(out, line, src, lineno);
    }
    fclose(fp);
}

/* Build full paths to the two data files */
static void make_paths(const char *data_dir,
                       char *vpath, size_t vsz,
                       char *qpath, size_t qsz) {
    snprintf(vpath, vsz, "%s/visited.txt", data_dir);
    snprintf(qpath, qsz, "%s/queue.txt",   data_dir);
}

/* Strip leading/trailing whitespace in-place */
static void trim(char *s) {
    /* leading */
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s) - i + 1);
    /* trailing */
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

/* ─── Public search functions ───────────────────────────────────── */

void search_by_keyword(const char *data_dir, const char *keyword,
                       SearchResultSet *out) {
    memset(out, 0, sizeof(*out));
    char vpath[PATH_LEN], qpath[PATH_LEN];
    make_paths(data_dir, vpath, sizeof(vpath), qpath, sizeof(qpath));

    scan_file(vpath, SRC_VISITED, (MatchFn)url_contains, keyword, out);
    scan_file(qpath, SRC_QUEUE,   (MatchFn)url_contains, keyword, out);
}

void search_by_url(const char *data_dir, const char *target_url,
                   SearchResultSet *out) {
    memset(out, 0, sizeof(*out));
    char vpath[PATH_LEN], qpath[PATH_LEN];
    make_paths(data_dir, vpath, sizeof(vpath), qpath, sizeof(qpath));

    scan_file(vpath, SRC_VISITED, (MatchFn)url_matches, target_url, out);
    scan_file(qpath, SRC_QUEUE,   (MatchFn)url_matches, target_url, out);
}

/*
 * search_by_file: each non-empty, non-comment line in file_path is
 * treated as a query.  Lines starting with '#' are skipped.
 * A line beginning with "url:" is an exact/prefix URL query.
 * Everything else is a keyword query.
 *
 * Example file:
 *   # find all blog posts
 *   /blog/
 *   url:https://example.com/about
 *   url:https://example.com/products/STAR   (STAR = wildcard *)
 */
void search_by_file(const char *data_dir, const char *file_path,
                    SearchResultSet *out) {
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        fprintf(stderr, "[SEARCH] Cannot open query file: %s (%s)\n",
                file_path, strerror(errno));
        return;
    }

    char vpath[PATH_LEN], qpath[PATH_LEN];
    make_paths(data_dir, vpath, sizeof(vpath), qpath, sizeof(qpath));

    char line[MAX_URL_LEN];
    int  query_no = 0;

    while (fgets(line, sizeof(line), fp) && out->count < MAX_RESULTS) {
        /* strip newline and whitespace */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]='\0';
        trim(line);
        if (len == 0 || line[0] == '#') continue;

        query_no++;
        printf("  [Query %d] %s\n", query_no, line);

        SearchResultSet partial;
        memset(&partial, 0, sizeof(partial));

        if (strncasecmp(line, "url:", 4) == 0) {
            /* explicit URL query */
            const char *target = line + 4;
            scan_file(vpath, SRC_VISITED, (MatchFn)url_matches, target, &partial);
            scan_file(qpath, SRC_QUEUE,   (MatchFn)url_matches, target, &partial);
        } else {
            /* keyword query */
            scan_file(vpath, SRC_VISITED, (MatchFn)url_contains, line, &partial);
            scan_file(qpath, SRC_QUEUE,   (MatchFn)url_contains, line, &partial);
        }

        /* merge partial into out */
        for (int i = 0; i < partial.count && out->count < MAX_RESULTS; i++) {
            /* deduplicate: skip if URL already in out */
            int dup = 0;
            for (int j = 0; j < out->count; j++) {
                if (strcmp(out->items[j].url, partial.items[i].url) == 0) {
                    dup = 1; break;
                }
            }
            if (!dup) {
                out->items[out->count] = partial.items[i];
                out->items[out->count].source = SRC_FILE;   /* tag origin */
                out->count++;
            }
        }
        if (partial.truncated) out->truncated = 1;
    }
    fclose(fp);
}

/* ─── Result display ─────────────────────────────────────────────── */

static const char *source_label(ResultSource s) {
    switch (s) {
        case SRC_VISITED: return "visited";
        case SRC_QUEUE:   return "queued ";
        case SRC_LOG:     return "log    ";
        case SRC_FILE:    return "file   ";
    }
    return "?      ";
}

void print_results(const SearchResultSet *r, const char *query) {
    printf("\n");
    printf("  ┌─────────────────────────────────────────────────────────────\n");
    printf("  │  Query : \"%s\"\n", query);
    printf("  │  Found : %d result(s)%s\n",
           r->count, r->truncated ? "  (truncated — more exist)" : "");
    printf("  ├─ SRC ─────┬──────────────────────────────────────────────────\n");

    if (r->count == 0) {
        printf("  │  (no matches)\n");
    } else {
        for (int i = 0; i < r->count; i++) {
            printf("  │ [%s] %s\n",
                   source_label(r->items[i].source),
                   r->items[i].url);
        }
    }
    printf("  └────────────────────────────────────────────────────────────\n\n");
}

/* ─── Menu helpers ──────────────────────────────────────────────── */

/* Read a line from stdin, strip newline, trim whitespace.
 * Returns 0 on EOF, 1 otherwise. */
static int read_input(const char *prompt, char *buf, size_t bufsz) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)bufsz, stdin)) return 0;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]='\0';
    trim(buf);
    return 1;
}

static void print_banner(const char *data_dir) {
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║              Web Crawler — Search Menu                  ║\n");
    printf("  ╠══════════════════════════════════════════════════════════╣\n");
    printf("  ║  Data dir : %-44s║\n", data_dir);
    printf("  ╠══════════════════════════════════════════════════════════╣\n");
    printf("  ║  1. Search by keyword  (substring in URL)               ║\n");
    printf("  ║  2. Search by URL      (exact or prefix with *)         ║\n");
    printf("  ║  3. Search by file     (one query per line)             ║\n");
    printf("  ║  4. Show stats                                          ║\n");
    printf("  ║  0. Exit                                                ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

static void show_stats(const CrawlerState *cs, const char *data_dir) {
    /* Count lines in visited.txt and queue.txt directly from disk */
    char vpath[PATH_LEN], qpath[PATH_LEN];
    snprintf(vpath, sizeof(vpath), "%s/visited.txt", data_dir);
    snprintf(qpath, sizeof(qpath), "%s/queue.txt",   data_dir);

    long visited_count = 0, queued_count = 0;

    FILE *fp = fopen(vpath, "r");
    if (fp) {
        char line[MAX_URL_LEN];
        while (fgets(line, sizeof(line), fp)) {
            size_t len = strlen(line);
            while (len>0&&(line[len-1]=='\n'||line[len-1]=='\r')) len--;
            if (len > 0) visited_count++;
        }
        fclose(fp);
    }

    fp = fopen(qpath, "r");
    if (fp) {
        char line[MAX_URL_LEN];
        while (fgets(line, sizeof(line), fp)) {
            size_t len = strlen(line);
            while (len>0&&(line[len-1]=='\n'||line[len-1]=='\r')) len--;
            if (len > 0) queued_count++;
        }
        fclose(fp);
    }

    printf("\n");
    printf("  ┌─── Crawl Statistics ──────────────────────────────────────\n");
    printf("  │  Data directory : %s\n", data_dir);
    printf("  │  Visited URLs   : %ld\n", visited_count);
    printf("  │  Queued URLs    : %ld (pending / frontier)\n", queued_count);

    if (cs) {
        long total_fetched = 0, total_links = 0, total_errors = 0;
        for (int i = 0; i < THREAD_COUNT; i++) {
            total_fetched += cs->stats[i].pages_fetched;
            total_links   += cs->stats[i].links_found;
            total_errors  += cs->stats[i].errors;
        }
        printf("  ├─── Thread Summary ────────────────────────────────────────\n");
        printf("  │  %-8s  %-12s  %-12s  %-8s\n",
               "Thread", "Pages fetched", "Links found", "Errors");
        printf("  │  %-8s  %-12s  %-12s  %-8s\n",
               "──────", "─────────────", "───────────", "──────");
        for (int i = 0; i < THREAD_COUNT; i++) {
            static const char *labels[] = {"A","B","C","D","E","F","G","H"};
            printf("  │  %-8s  %-12ld  %-12ld  %-8ld\n",
                   labels[i],
                   cs->stats[i].pages_fetched,
                   cs->stats[i].links_found,
                   cs->stats[i].errors);
        }
        printf("  ├────────────────────────────────────────────────────────────\n");
        printf("  │  %-8s  %-12ld  %-12ld  %-8ld\n",
               "TOTAL", total_fetched, total_links, total_errors);
    }
    printf("  └────────────────────────────────────────────────────────────\n\n");
}

/* ─── Public menu entry point ────────────────────────────────────── */

void run_menu(CrawlerState *cs, const char *data_dir) {
    char input[MAX_URL_LEN];
    SearchResultSet results;

    while (1) {
        print_banner(data_dir);

        if (!read_input("  Choice> ", input, sizeof(input))) break;
        if (input[0] == '\0') continue;

        switch (input[0]) {

        /* ── 1. Keyword search ─────────────────────────────────── */
        case '1': {
            char keyword[MAX_URL_LEN];
            if (!read_input("  Keyword> ", keyword, sizeof(keyword))) break;
            if (keyword[0] == '\0') {
                printf("  [!] Empty keyword — showing all URLs.\n");
            }
            search_by_keyword(data_dir, keyword, &results);
            print_results(&results, keyword);
            break;
        }

        /* ── 2. URL search ─────────────────────────────────────── */
        case '2': {
            char url[MAX_URL_LEN];
            printf("  Tip: append * for prefix match  "
                   "(e.g. https://example.com/blog/*)\n");
            if (!read_input("  URL> ", url, sizeof(url))) break;
            if (url[0] == '\0') { printf("  [!] Empty input.\n"); break; }
            search_by_url(data_dir, url, &results);
            print_results(&results, url);
            break;
        }

        /* ── 3. File search ────────────────────────────────────── */
        case '3': {
            char fpath[PATH_LEN];
            printf("  File format: one query per line.\n");
            printf("    keyword  -> substring match\n");
            printf("    url:<u>  -> exact/prefix URL match\n");
            printf("    # ...    -> comment\n");
            if (!read_input("  File path> ", fpath, sizeof(fpath))) break;
            if (fpath[0] == '\0') { printf("  [!] Empty path.\n"); break; }

            printf("\n  Running queries from: %s\n", fpath);
            search_by_file(data_dir, fpath, &results);
            print_results(&results, fpath);
            break;
        }

        /* ── 4. Stats ──────────────────────────────────────────── */
        case '4':
            show_stats(cs, data_dir);
            break;

        /* ── 0 / q — exit ─────────────────────────────────────── */
        case '0':
        case 'q':
        case 'Q':
            printf("  Goodbye.\n\n");
            return;

        default:
            printf("  [!] Unknown option '%s' — try 1, 2, 3, 4, or 0.\n\n",
                   input);
        }

        /* pause before redrawing the menu */
        read_input("  Press Enter to continue...", input, sizeof(input));
    }
}