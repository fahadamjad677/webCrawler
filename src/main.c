#include "crawler.h"
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

/* ── Global crawler state (static — one instance) ─────────────── */
static CrawlerState g_cs;

/* Forward declaration from worker.c */
void launch_workers(CrawlerState *cs, pthread_t threads[]);
void build_data_paths(CrawlerState *cs, const char *seed_url);  /* <-- ADD THIS LINE */

/* ── Sanitize a host string into a folder-safe name ────────────── */
static void sanitize_name(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outlen - 1; i++) {
        char c = in[i];
        if (c == '/' || c == ':' || c == '\\' || c == '?' ||
            c == '*' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

/* ── Graceful SIGINT handler ──────────────────────────────────── */
// main.c
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[SYSTEM] Interrupt — will save and exit after current fetches...\n");
    atomic_store(&g_cs.sigint_received, 1);
}

/* ── Ensure a directory path exists (mkdir -p style) ────────────── */
static void ensure_dir(const char *path) {
    char tmp[PATH_LEN];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                perror("mkdir");
                exit(1);
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        exit(1);
    }
}

/* ── Build data/<site>/visited.txt and data/<site>/crawler.log ──── */
void build_data_paths(CrawlerState *cs, const char *seed_url) {
    /* strip scheme */
    const char *sep = strstr(seed_url, "://");
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

/* ── Extract scheme+host from a URL into out ─────────────────── */
static void extract_origin(const char *url, char *out, size_t outlen) {
    const char *sep = strstr(url, "://");
    if (!sep) { strncpy(out, url, outlen - 1); out[outlen-1] = '\0'; return; }
    sep += 3;
    const char *slash = strchr(sep, '/');
    size_t host_len = slash ? (size_t)(slash - url) : strlen(url);
    size_t copy = host_len < outlen - 1 ? host_len : outlen - 1;
    memcpy(out, url, copy);
    out[copy] = '\0';
}

/* ── Default seed URLs ─────────────────────────────────────────── */
static const char *DEFAULT_SEEDS[] = {
    "https://example.com",
    NULL
};

int main(int argc, char *argv[]) {
    /* ── Init global state ──────────────────────────────────── */
    memset(&g_cs, 0, sizeof(g_cs));
    queue_init(&g_cs.queue);
    visited_init(&g_cs.visited);
    pthread_mutex_init(&g_cs.lock,           NULL);
    pthread_cond_init (&g_cs.work_available, NULL);

    /* ── Set seed origin for domain filter ─────────────────── */
    const char *first_seed = (argc > 1) ? argv[1] : DEFAULT_SEEDS[0];
    extract_origin(first_seed, g_cs.seed_origin, sizeof(g_cs.seed_origin));

    /* ── Build data/<site>/ paths and ensure the dir exists ──── */
    build_data_paths(&g_cs, g_cs.seed_origin);
    ensure_dir(g_cs.data_dir);

    /* open log file */
    g_cs.logfp = fopen(g_cs.log_file, "a");
    if (!g_cs.logfp) perror("[MAIN] Warning: cannot open log file");

    /* register signal handler for Ctrl+C */
    signal(SIGINT, handle_sigint);

    printf("[SYSTEM] Domain filter : %s\n", g_cs.seed_origin);
    printf("[SYSTEM] Data dir      : %s\n", g_cs.data_dir);
    printf("[SYSTEM] Page cap      : %d pages\n\n", MAX_PAGES);

    /* ── Resume: load previously visited URLs ──────────────── */
int loaded = load_visited(&g_cs.visited, &g_cs.queue, g_cs.data_dir);
    if (loaded > 0) {
        printf("[SYSTEM] Resuming — loaded %d previously visited URLs\n",
               loaded);
    } else {
        printf("[SYSTEM] No resume file found — starting fresh crawl\n");
    }

    /* ── Seed the queue ────────────────────────────────────── */
    int seeds_queued = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!visited_contains(&g_cs.visited, argv[i])) {
                queue_push(&g_cs.queue, argv[i]);
                seeds_queued++;
            }
        }
    } else {
        for (int i = 0; DEFAULT_SEEDS[i]; i++) {
            if (!visited_contains(&g_cs.visited, DEFAULT_SEEDS[i])) {
                queue_push(&g_cs.queue, DEFAULT_SEEDS[i]);
                seeds_queued++;
            }
        }
    }

    if (seeds_queued == 0 && queue_is_empty(&g_cs.queue)) {
        printf("[SYSTEM] All seed URLs already visited. "
               "Pass new URLs as arguments or delete '%s' to re-crawl.\n",
               g_cs.save_file);
        return 0;
    }

    printf("[SYSTEM] Starting %d worker threads\n\n", THREAD_COUNT);

    /* ── Launch threads ─────────────────────────────────────── */
    pthread_t threads[THREAD_COUNT];
    launch_workers(&g_cs, threads);

    /* ── Wait for all threads to finish ─────────────────────── */
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    /* ── Final save ─────────────────────────────────────────── */
    pthread_mutex_lock(&g_cs.lock);
    save_visited(&g_cs.visited, &g_cs.queue, g_cs.data_dir);
    pthread_mutex_unlock(&g_cs.lock);

    /* ── Print summary ──────────────────────────────────────── */
    print_stats(&g_cs);

    /* ── Cleanup ─────────────────────────────────────────────── */
    pthread_mutex_destroy(&g_cs.lock);
    pthread_cond_destroy(&g_cs.work_available);
    if (g_cs.logfp) fclose(g_cs.logfp);

    curl_global_cleanup();

    return 0;
}