#include "crawler.h"

/* Argument bundle passed to each worker thread */
typedef struct {
    CrawlerState *cs;
    int           thread_id;
} WorkerArg;

/* ─── Domain filter ───────────────────────────────────────────── */

/*
 * get_origin: copy scheme+host from a URL into out.
 * e.g. "https://example.com/foo/bar" -> "https://example.com"
 */
static void get_origin(const char *url, char *out, size_t outlen) {
    const char *sep = strstr(url, "://");
    if (!sep) { strncpy(out, url, outlen - 1); out[outlen-1] = '\0'; return; }
    sep += 3;
    const char *slash = strchr(sep, '/');
    size_t host_len = slash ? (size_t)(slash - url) : strlen(url);
    size_t copy = host_len < outlen - 1 ? host_len : outlen - 1;
    memcpy(out, url, copy);
    out[copy] = '\0';
}

/*
 * same_origin: returns 1 if `url` belongs to the same scheme+host
 * as cs->seed_origin, 0 otherwise.
 *
 * This is the primary mechanism that keeps the crawl finite.
 */
static int same_origin(const CrawlerState *cs, const char *url) {
    if (cs->seed_origin[0] == '\0') return 1; /* no filter — allow all */
    char url_origin[MAX_URL_LEN];
    get_origin(url, url_origin, sizeof(url_origin));
    return strcmp(cs->seed_origin, url_origin) == 0;
}

/* ─── Worker thread ───────────────────────────────────────────── */

void *worker_thread(void *arg) {
    WorkerArg    *wa = (WorkerArg *)arg;
    CrawlerState *cs = wa->cs;
    int           id = wa->thread_id;

    char url[MAX_URL_LEN];
    char new_links[MAX_LINKS][MAX_URL_LEN];

    while (1) {
        /* ── 1. Lock and wait for work ─────────────────────────── */
        pthread_mutex_lock(&cs->lock);

        while (queue_is_empty(&cs->queue) && !cs->shutdown) {
            if (cs->active_threads == 0) {
                /* Queue empty, nobody fetching — crawl is complete. */
                cs->shutdown = 1;
                pthread_cond_broadcast(&cs->work_available);
                break;
            }
            pthread_cond_wait(&cs->work_available, &cs->lock);
        }

        if (cs->shutdown && queue_is_empty(&cs->queue)) {
            pthread_mutex_unlock(&cs->lock);
            pthread_cond_broadcast(&cs->work_available);
            break;
        }

        /* ── 2. Pop URL ───────────────────────────────────────── */
        if (!queue_pop(&cs->queue, url)) {
            pthread_mutex_unlock(&cs->lock);
            continue;
        }

        /* Skip if already visited (duplicate in queue) */
        if (visited_contains(&cs->visited, url)) {
            pthread_mutex_unlock(&cs->lock);
            continue;
        }

        cs->active_threads++;
        log_msg(cs, id, "Fetching  --> %s  [queue: %d]",
                url, queue_size(&cs->queue));

        pthread_mutex_unlock(&cs->lock);

        /* ── 3. Fetch (outside lock) ──────────────────────────── */
        ResponseBuf buf = {NULL, 0};
        int fetch_err = fetch_url(url, &buf);

        /* ── 4. Parse links (outside lock) ───────────────────── */
        int link_count = 0;
        if (!fetch_err && buf.data) {
            link_count = extract_links(url, buf.data, buf.len,
                                       new_links, MAX_LINKS);
        }
        response_buf_free(&buf);

        /* ── 5. Update shared state (inside lock) ─────────────── */
        pthread_mutex_lock(&cs->lock);

        if (fetch_err) {
            log_msg(cs, id, "ERROR     --> %s  (curl code %d)", url, fetch_err);
            cs->stats[id].errors++;
        } else {
            visited_insert(&cs->visited, url);
            cs->stats[id].pages_fetched++;

            log_msg(cs, id, "Done      <-- %s  [%d links found]", url, link_count);

            /* ── 6. Page cap check ────────────────────────────── */
            if (visited_count(&cs->visited) >= MAX_PAGES) {
                log_msg(cs, id,
                        "[LIMIT] Reached %d-page cap — shutting down.",
                        MAX_PAGES);
                cs->shutdown = 1;
                pthread_cond_broadcast(&cs->work_available);
                goto update_done;
            }

            /* ── 7. Enqueue new same-origin, unseen links ──────── */
            int added = 0;
            for (int i = 0; i < link_count; i++) {
                /* Domain filter: drop links that leave the seed host */
                if (!same_origin(cs, new_links[i])) continue;

                if (!visited_contains(&cs->visited, new_links[i])) {
                    if (queue_push(&cs->queue, new_links[i])) {
                        added++;
                        cs->stats[id].links_found++;
                    }
                }
            }

            if (added > 0) {
                log_msg(cs, id, "Queued    +++ %d new links", added);
                pthread_cond_broadcast(&cs->work_available);
            }

            /* ── 8. Save progress ─────────────────────────────── */
            if (save_visited(&cs->visited,  cs->save_file) == 0) {
                log_msg(cs, id, "[SAVING] Progress saved (%d visited)",
                        visited_count(&cs->visited));
            }
        }

update_done:
        /* Decrement AFTER all work is done, then check termination */
        cs->active_threads--;

        if (queue_is_empty(&cs->queue) && cs->active_threads == 0) {
            cs->shutdown = 1;
            pthread_cond_broadcast(&cs->work_available);
        }

        pthread_mutex_unlock(&cs->lock);

        /* ── 9. Polite delay (outside lock) ───────────────────── */
        if (!cs->shutdown)
            usleep(FETCH_DELAY_MS * 1000);
    }

    return NULL;
}

/* ── Entry point used by main to spawn threads ───────────────── */

static WorkerArg worker_args[THREAD_COUNT];

void launch_workers(CrawlerState *cs, pthread_t threads[]) {
    for (int i = 0; i < THREAD_COUNT; i++) {
        worker_args[i].cs        = cs;
        worker_args[i].thread_id = i;
        pthread_create(&threads[i], NULL, worker_thread, &worker_args[i]);
    }
}