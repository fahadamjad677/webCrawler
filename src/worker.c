#include "crawler.h"

/* Argument bundle passed to each worker thread */
typedef struct {
    CrawlerState *cs;
    int           thread_id;
} WorkerArg;

/*
 * worker_thread: the main crawl loop for each thread.
 *
 * Algorithm:
 *   1. Lock mutex
 *   2. Wait (cond_wait) until queue is non-empty OR shutdown flag set
 *   3. Pop a URL; mark active
 *   4. Unlock mutex (let other threads work in parallel)
 *   5. Fetch URL (network I/O — no lock held)
 *   6. Parse HTML for links
 *   7. Lock mutex again
 *   8. Mark URL visited, enqueue new links, save progress
 *   9. Signal other threads, unlock, sleep politely
 *  10. Repeat until queue empty AND no other thread is active
 */
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
            /* If queue is empty but other threads are still fetching,
             * they might discover new URLs — so wait for a signal.
             * If no threads are active either, we're done.             */
            if (cs->active_threads == 0) {
                cs->shutdown = 1;   /* signal everyone else to stop    */
                break;
            }
            pthread_cond_wait(&cs->work_available, &cs->lock);
        }

        if (cs->shutdown && queue_is_empty(&cs->queue)) {
            pthread_mutex_unlock(&cs->lock);
            pthread_cond_broadcast(&cs->work_available); /* wake sleeping threads */
            break;
        }

        /* ── 2. Pop URL ───────────────────────────────────────── */
        if (!queue_pop(&cs->queue, url)) {
            pthread_mutex_unlock(&cs->lock);
            continue;
        }

        /* skip if already visited (could have been added twice before
         * either thread processed it)                                  */
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
            /* mark this URL as visited */
            visited_insert(&cs->visited, url);
            cs->stats[id].pages_fetched++;

            log_msg(cs, id, "Done      <-- %s  [%d links found]", url, link_count);

            /* enqueue new, unseen links */
            int added = 0;
            for (int i = 0; i < link_count; i++) {
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

            /* ── 6. Save progress (persist while holding lock) ── */
            if (save_visited(&cs->visited, SAVE_FILE) == 0) {
                log_msg(cs, id, "[SAVING] Progress saved to '%s'  "
                        "(%d visited)", SAVE_FILE,
                        visited_count(&cs->visited));
            }
        }

        cs->active_threads--;

        /* If we just emptied the queue and no one else is running,
         * wake everyone so they can detect the done condition.         */
        if (queue_is_empty(&cs->queue) && cs->active_threads == 0) {
            cs->shutdown = 1;
            pthread_cond_broadcast(&cs->work_available);
        }

        pthread_mutex_unlock(&cs->lock);

        /* ── 7. Polite delay (outside lock) ──────────────────── */
        usleep(FETCH_DELAY_MS * 1000);
    }

    return NULL;
}

/* ── Entry point used by main to spawn threads ──────────────────── */

/* storage for args — one per thread, static lifetime */
static WorkerArg worker_args[THREAD_COUNT];

void launch_workers(CrawlerState *cs, pthread_t threads[]) {
    for (int i = 0; i < THREAD_COUNT; i++) {
        worker_args[i].cs        = cs;
        worker_args[i].thread_id = i;
        pthread_create(&threads[i], NULL, worker_thread, &worker_args[i]);
    }
}
