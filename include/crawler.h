#ifndef CRAWLER_H
#define CRAWLER_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>

/* ─── tunables ─────────────────────────────────────────────────── */
#define MAX_URLS        2000
#define MAX_URL_LEN     512
#define MAX_LINKS       50      /* max links extracted per page    */
#define THREAD_COUNT    4
#define DATA_ROOT       "data"
#define FETCH_DELAY_MS  500     /* polite crawl delay per thread   */
#define QUEUE_CAPACITY  MAX_URLS
#define MAX_PAGES       500     /* stop crawl after this many pages */
#define PATH_LEN        (MAX_URL_LEN + 64)

/* ─── URL queue (circular, bounded) ────────────────────────────── */
typedef struct {
    char  items[QUEUE_CAPACITY][MAX_URL_LEN];
    int   head;
    int   tail;
    int   size;
} URLQueue;

/* ─── Visited-URL set (linear-probe hash table) ─────────────────── */
#define VISITED_CAPACITY  4096   /* must be power of 2 */
typedef struct {
    char  table[VISITED_CAPACITY][MAX_URL_LEN];
    int   occupied[VISITED_CAPACITY];
    int   count;
} VisitedSet;

/* ─── HTTP response buffer ───────────────────────────────────────── */
typedef struct {
    char  *data;
    size_t len;
} ResponseBuf;

/* ─── Per-thread statistics ──────────────────────────────────────── */
typedef struct {
    int   thread_id;
    long  pages_fetched;
    long  links_found;
    long  errors;
} ThreadStats;

/* ─── Global crawler state ───────────────────────────────────────── */
typedef struct {
    URLQueue       queue;
    VisitedSet     visited;
    pthread_mutex_t lock;
    pthread_cond_t  work_available;  /* signaled when queue non-empty */
    int             active_threads;  /* threads currently fetching    */
    int             shutdown;        /* set to 1 to stop all threads  */
    ThreadStats     stats[THREAD_COUNT];
    FILE           *logfp;
    char            seed_origin[MAX_URL_LEN]; /* scheme+host of first seed */
    char            data_dir[PATH_LEN];       /* data/<site>          <-- ADD THIS LINE */
    char            save_file[PATH_LEN];      /* data/<site>/visited.txt */
    char            log_file[PATH_LEN];       /* data/<site>/crawler.log */
} CrawlerState;

/* ─── Function prototypes ────────────────────────────────────────── */

/* queue.c */
void  queue_init(URLQueue *q);
int   queue_push(URLQueue *q, const char *url);
int   queue_pop(URLQueue *q, char *out);
int   queue_size(const URLQueue *q);
int   queue_is_empty(const URLQueue *q);

/* visited.c */
void  visited_init(VisitedSet *v);
int   visited_contains(const VisitedSet *v, const char *url);
int   visited_insert(VisitedSet *v, const char *url);
int   visited_count(const VisitedSet *v);

/* fetcher.c */
int   fetch_url(const char *url, ResponseBuf *buf);
void  response_buf_free(ResponseBuf *buf);

/* parser.c */
int   extract_links(const char *base_url,
                    const char *html, size_t html_len,
                    char links[][MAX_URL_LEN], int max_links);

/* persistence.c */
int   save_visited(const VisitedSet *v, const char *path);
int   load_visited(VisitedSet *v, URLQueue *q, const char *path);

/* logger.c */
void  log_msg(CrawlerState *cs, int thread_id, const char *fmt, ...);

/* worker.c */
void *worker_thread(void *arg);

/* stats.c */
void  print_stats(const CrawlerState *cs);

/* main.c (used by worker.c) */
void launch_workers(CrawlerState *cs, pthread_t threads[]);

#endif /* CRAWLER_H */