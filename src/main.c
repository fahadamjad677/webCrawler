#include "crawler.h"
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

/* ── Global crawler state (static — one instance) ─────────────── */
static CrawlerState g_cs;

/* Forward declaration from worker.c */
void launch_workers(CrawlerState *cs, pthread_t threads[]);

/* ── Graceful SIGINT handler ──────────────────────────────────── */
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[SYSTEM] Interrupt received — saving state and shutting down...\n");

    pthread_mutex_lock(&g_cs.lock);
    g_cs.shutdown = 1;
    save_visited(&g_cs.visited, SAVE_FILE);
    pthread_cond_broadcast(&g_cs.work_available);
    pthread_mutex_unlock(&g_cs.lock);
}

/* ── Ensure data/ directory exists ───────────────────────────── */
static void ensure_data_dir(void) {
    if (mkdir("data", 0755) != 0 && errno != EEXIST) {
        perror("mkdir data");
        exit(1);
    }
}

/* ── Default seed URLs ─────────────────────────────────────────── */
static const char *DEFAULT_SEEDS[] = {
    "https://example.com",
    "https://www.iana.org",
    NULL
};

int main(int argc, char *argv[]) {
    ensure_data_dir();

    /* ── Init global state ──────────────────────────────────── */
    memset(&g_cs, 0, sizeof(g_cs));
    queue_init(&g_cs.queue);
    visited_init(&g_cs.visited);
    pthread_mutex_init(&g_cs.lock,           NULL);
    pthread_cond_init (&g_cs.work_available, NULL);

    /* open log file */
    g_cs.logfp = fopen(LOG_FILE, "a");
    if (!g_cs.logfp) perror("[MAIN] Warning: cannot open log file");

    /* register signal handler for Ctrl+C */
    signal(SIGINT, handle_sigint);

    /* ── Resume: load previously visited URLs ──────────────── */
    int loaded = load_visited(&g_cs.visited, &g_cs.queue, SAVE_FILE);
    if (loaded > 0) {
        printf("[SYSTEM] Loading '%s'... %d URLs found\n", SAVE_FILE, loaded);
    } else {
        printf("[SYSTEM] No resume file found — starting fresh crawl\n");
    }

    /* ── Seed the queue ────────────────────────────────────── */
    int seeds_queued = 0;
    if (argc > 1) {
        /* URLs from command line: ./crawler https://example.com ... */
        for (int i = 1; i < argc; i++) {
            if (!visited_contains(&g_cs.visited, argv[i])) {
                queue_push(&g_cs.queue, argv[i]);
                seeds_queued++;
            }
        }
    } else {
        /* default seeds */
        for (int i = 0; DEFAULT_SEEDS[i]; i++) {
            if (!visited_contains(&g_cs.visited, DEFAULT_SEEDS[i])) {
                queue_push(&g_cs.queue, DEFAULT_SEEDS[i]);
                seeds_queued++;
            }
        }
    }

    if (seeds_queued == 0 && queue_is_empty(&g_cs.queue)) {
        printf("[SYSTEM] All seed URLs already visited. "
               "Pass new URLs as arguments.\n");
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
    save_visited(&g_cs.visited, SAVE_FILE);
    pthread_mutex_unlock(&g_cs.lock);

    /* ── Print summary ──────────────────────────────────────── */
    print_stats(&g_cs);

    /* ── Cleanup ─────────────────────────────────────────────── */
    pthread_mutex_destroy(&g_cs.lock);
    pthread_cond_destroy(&g_cs.work_available);
    if (g_cs.logfp) fclose(g_cs.logfp);

    /* libcurl global cleanup */
    curl_global_cleanup();

    return 0;
}
