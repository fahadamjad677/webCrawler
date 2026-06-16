#include "crawler.h"
#include "filter.h"
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

/* ── Global crawler state ─────────────────────────────────────── */
static CrawlerState  g_cs;
static CrawlFilter   g_filter;   /* lives for the whole process   */

/* ── Forward declarations ─────────────────────────────────────── */
void launch_workers(CrawlerState *cs, pthread_t threads[]);
void build_data_paths(CrawlerState *cs, const char *seed_url);

/* ── Sanitize a host string into a folder-safe name ────────────── */
static void sanitize_name(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outlen - 1; i++) {
        char c = in[i];
        if (c=='/'||c==':'||c=='\\'||c=='?'||
            c=='*'||c=='"'||c=='<'||c=='>'||c=='|')
            c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

/* ── Graceful SIGINT handler ──────────────────────────────────── */
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[SYSTEM] Interrupt — saving and exiting after current fetches...\n");
    atomic_store(&g_cs.sigint_received, 1);
}

/* ── Ensure directory exists (mkdir -p) ──────────────────────── */
static void ensure_dir(const char *path) {
    char tmp[PATH_LEN];
    strncpy(tmp, path, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    for (char *p = tmp+1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) { perror("mkdir"); exit(1); }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) { perror("mkdir"); exit(1); }
}

/* ── Build data/<site>/ paths ────────────────────────────────── */
void build_data_paths(CrawlerState *cs, const char *seed_url) {
    const char *sep   = strstr(seed_url, "://");
    const char *start = sep ? sep + 3 : seed_url;
    char clean[MAX_URL_LEN];
    sanitize_name(start, clean, sizeof(clean));
    snprintf(cs->data_dir,  sizeof(cs->data_dir),  "%s/%s", DATA_ROOT, clean);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(cs->save_file, sizeof(cs->save_file), "%s/visited.txt", cs->data_dir);
    snprintf(cs->log_file,  sizeof(cs->log_file),  "%s/crawler.log", cs->data_dir);
#pragma GCC diagnostic pop
}

/* ── Extract scheme+host from URL ────────────────────────────── */
static void extract_origin(const char *url, char *out, size_t outlen) {
    const char *sep = strstr(url, "://");
    if (!sep) { strncpy(out, url, outlen-1); out[outlen-1]='\0'; return; }
    sep += 3;
    const char *slash = strchr(sep, '/');
    size_t host_len = slash ? (size_t)(slash-url) : strlen(url);
    size_t copy = host_len < outlen-1 ? host_len : outlen-1;
    memcpy(out, url, copy);
    out[copy] = '\0';
}

/* ── Read a line from stdin, strip whitespace ─────────────────── */
static int read_input(const char *prompt, char *buf, size_t bufsz) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)bufsz, stdin)) return 0;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1]=='\n'||buf[len-1]=='\r'||buf[len-1]==' '))
        buf[--len] = '\0';
    return 1;
}

/* ── Run the crawl with whatever filter is set ───────────────── */
static void run_crawl(const char *seed_url) {
    /* ── Reset crawl state (allow re-runs within same session) ── */
    queue_init(&g_cs.queue);
    visited_init(&g_cs.visited);
    g_cs.shutdown        = 0;
    g_cs.active_threads  = 0;
    atomic_store(&g_cs.sigint_received, 0);
    for (int i = 0; i < THREAD_COUNT; i++) {
        g_cs.stats[i].pages_fetched = 0;
        g_cs.stats[i].links_found   = 0;
        g_cs.stats[i].errors        = 0;
    }

    /* ── Resume: load previously visited URLs ─────────────────── */
    int loaded = load_visited(&g_cs.visited, &g_cs.queue, g_cs.data_dir);
    if (loaded > 0)
        printf("\n[SYSTEM] Resuming — loaded %d previously visited URLs\n", loaded);
    else
        printf("\n[SYSTEM] Starting fresh crawl\n");

    /* ── Seed the queue (only if it passes the filter) ────────── */
    int seeds_queued = 0;
    if (!visited_contains(&g_cs.visited, seed_url) &&
        filter_matches(&g_filter, seed_url)) {
        queue_push(&g_cs.queue, seed_url);
        seeds_queued++;
    }

    if (seeds_queued == 0 && queue_is_empty(&g_cs.queue)) {
        printf("[SYSTEM] Seed URL does not match the active filter — nothing to crawl.\n");
        printf("         Seed : %s\n", seed_url);
        char desc[256]; filter_describe(&g_filter, desc, sizeof(desc));
        printf("         Filter: %s\n\n", desc);
        return;
    }

    /* ── Describe what we're about to do ─────────────────────── */
    char desc[256];
    filter_describe(&g_filter, desc, sizeof(desc));
    printf("[SYSTEM] Filter        : %s\n",   desc);
    printf("[SYSTEM] Domain filter : %s\n",   g_cs.seed_origin);
    printf("[SYSTEM] Data dir      : %s\n",   g_cs.data_dir);
    printf("[SYSTEM] Page cap      : %d pages\n", MAX_PAGES);
    printf("[SYSTEM] Starting %d worker threads\n\n", THREAD_COUNT);

    /* ── Register signal handler ─────────────────────────────── */
    signal(SIGINT, handle_sigint);

    /* ── Launch threads ──────────────────────────────────────── */
    pthread_t threads[THREAD_COUNT];
    launch_workers(&g_cs, threads);

    for (int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threads[i], NULL);

    /* ── Final save + summary ────────────────────────────────── */
    pthread_mutex_lock(&g_cs.lock);
    save_visited(&g_cs.visited, &g_cs.queue, g_cs.data_dir);
    pthread_mutex_unlock(&g_cs.lock);

    print_stats(&g_cs);
}

/* ── Print the main menu ─────────────────────────────────────── */
static void print_menu(const char *seed_url) {
    char desc[256];
    filter_describe(&g_filter, desc, sizeof(desc));

    printf("\n");
    printf("  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║                   Web Crawler Menu                      ║\n");
    printf("  ╠══════════════════════════════════════════════════════════╣\n");
    printf("  ║  Seed   : %-46s║\n", seed_url);
    printf("  ║  Filter : %-46s║\n", desc);
    printf("  ╠══════════════════════════════════════════════════════════╣\n");
    printf("  ║  1. Crawl all URLs                                      ║\n");
    printf("  ║  2. Crawl URLs matching a keyword                       ║\n");
    printf("  ║  3. Crawl URLs matching a URL pattern                   ║\n");
    printf("  ║  4. Crawl URLs from a filter file                       ║\n");
    printf("  ║  5. Start crawl  (use current filter)                   ║\n");
    printf("  ╠══════════════════════════════════════════════════════════╣\n");
    printf("  ║  0. Exit                                                ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* ── Main ────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <seed-url>\n", argv[0]);
        return 1;
    }
    const char *seed_url = argv[1];

    /* ── Init global state ───────────────────────────────────── */
    memset(&g_cs,     0, sizeof(g_cs));
    memset(&g_filter, 0, sizeof(g_filter));
    g_filter.type = FILTER_ALL;   /* default: crawl everything */

    pthread_mutex_init(&g_cs.lock,           NULL);
    pthread_cond_init (&g_cs.work_available, NULL);
    g_cs.filter = &g_filter;

    extract_origin(seed_url, g_cs.seed_origin, sizeof(g_cs.seed_origin));
    build_data_paths(&g_cs, g_cs.seed_origin);
    ensure_dir(g_cs.data_dir);

    g_cs.logfp = fopen(g_cs.log_file, "a");
    if (!g_cs.logfp) perror("[MAIN] Warning: cannot open log file");

    /* ── Menu loop ───────────────────────────────────────────── */
    char input[MAX_URL_LEN];

    while (1) {
        print_menu(seed_url);

        if (!read_input("  Choice> ", input, sizeof(input))) break;
        if (input[0] == '\0') continue;

        switch (input[0]) {

        /* ── 1. Crawl all ──────────────────────────────────── */
        case '1':
            g_filter.type = FILTER_ALL;
            g_filter.pattern[0] = '\0';
            run_crawl(seed_url);
            break;

        /* ── 2. Keyword filter ─────────────────────────────── */
        case '2': {
            char kw[MAX_URL_LEN];
            if (!read_input("  Keyword> ", kw, sizeof(kw))) break;
            if (kw[0] == '\0') { printf("  [!] Empty keyword.\n"); break; }
            g_filter.type = FILTER_KEYWORD;
            strncpy(g_filter.pattern, kw, MAX_URL_LEN-1);
            g_filter.pattern[MAX_URL_LEN-1] = '\0';
            run_crawl(seed_url);
            break;
        }

        /* ── 3. URL pattern filter ─────────────────────────── */
        case '3': {
            char pat[MAX_URL_LEN];
            printf("  Tip: append * for prefix  (e.g. https://example.com/blog/*)\n");
            if (!read_input("  URL pattern> ", pat, sizeof(pat))) break;
            if (pat[0] == '\0') { printf("  [!] Empty pattern.\n"); break; }
            g_filter.type = FILTER_URL;
            strncpy(g_filter.pattern, pat, MAX_URL_LEN-1);
            g_filter.pattern[MAX_URL_LEN-1] = '\0';
            run_crawl(seed_url);
            break;
        }

        /* ── 4. File filter ────────────────────────────────── */
        case '4': {
            char fpath[PATH_LEN];
            printf("  File format: one pattern per line.\n");
            printf("    keyword     -> substring match\n");
            printf("    url:<pat>   -> exact / prefix URL match\n");
            printf("    # comment   -> ignored\n");
            if (!read_input("  File path> ", fpath, sizeof(fpath))) break;
            if (fpath[0] == '\0') { printf("  [!] Empty path.\n"); break; }

            int n = filter_load_file(&g_filter, fpath);
            if (n < 0) break;   /* error already printed */

            g_filter.type = FILTER_FILE;
            strncpy(g_filter.pattern, fpath, MAX_URL_LEN-1);
            g_filter.pattern[MAX_URL_LEN-1] = '\0';
            printf("  Loaded %d pattern(s) from %s\n", n, fpath);
            run_crawl(seed_url);
            break;
        }

        /* ── 5. Start with current filter ─────────────────── */
        case '5':
            run_crawl(seed_url);
            break;

        /* ── 0 / q — exit ─────────────────────────────────── */
        case '0':
        case 'q':
        case 'Q':
            printf("  Goodbye.\n\n");
            goto done;

        default:
            printf("  [!] Unknown option — try 1-5 or 0.\n");
        }

        read_input("\n  Press Enter to continue...", input, sizeof(input));
    }

done:
    pthread_mutex_destroy(&g_cs.lock);
    pthread_cond_destroy(&g_cs.work_available);
    if (g_cs.logfp) fclose(g_cs.logfp);
    curl_global_cleanup();
    return 0;
}