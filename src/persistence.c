#include "crawler.h"
#include <sys/stat.h>
#include <errno.h>

/*
 * save_visited: write all visited URLs to `path`, one per line.
 * Called periodically (under the mutex) to persist crawler state.
 *
 * Uses a temp file + rename for atomic write — no half-written file.
 */



int save_visited(const VisitedSet *v, const URLQueue *q, const char *dir) {
    // --- save visited.txt (existing logic, unchanged) ---
    char vpath[PATH_LEN], tmp[PATH_LEN];
    snprintf(vpath, sizeof(vpath), "%s/visited.txt", dir);
    snprintf(tmp,   sizeof(tmp),   "%s/visited.txt.tmp", dir);

    FILE *fp = fopen(tmp, "w");
    if (!fp) { perror("[SAVE] visited fopen"); return -1; }
    for (int i = 0; i < VISITED_CAPACITY; i++)
        if (v->occupied[i]) fprintf(fp, "%s\n", v->table[i]);
    fflush(fp); fclose(fp);
    if (rename(tmp, vpath) != 0) { perror("[SAVE] visited rename"); return -1; }

    // --- save queue.txt  (NEW) ---
    char qpath[PATH_LEN], qtmp[PATH_LEN];
    snprintf(qpath, sizeof(qpath), "%s/queue.txt", dir);
    snprintf(qtmp,  sizeof(qtmp),  "%s/queue.txt.tmp", dir);

    FILE *qp = fopen(qtmp, "w");
    if (!qp) { perror("[SAVE] queue fopen"); return -1; }

    // iterate circular queue without popping
    int head = q->head;
    for (int i = 0; i < q->size; i++) {
        int idx = (head + i) % QUEUE_CAPACITY;
        fprintf(qp, "%s\n", q->items[idx]);
    }
    fflush(qp); fclose(qp);
    if (rename(qtmp, qpath) != 0) { perror("[SAVE] queue rename"); return -1; }

    return 0;
}

/*
 * load_visited: read previously-saved URLs from `path` into the
 * visited set AND re-enqueue them so we don't revisit.
 * But actually — we just mark them visited; they've already been crawled.
 *
 * We also read an optional "queue.txt" if the crawler crashed mid-run,
 * but that's handled by the seed URL approach in main.
 *
 * Returns number of URLs loaded, -1 on error.
 */
int load_visited(VisitedSet *v, URLQueue *q, const char *dir) {
    char vpath[PATH_LEN], qpath[PATH_LEN];
    snprintf(vpath, sizeof(vpath), "%s/visited.txt", dir);
    snprintf(qpath, sizeof(qpath), "%s/queue.txt",   dir);

    // --- load visited.txt ---
    FILE *fp = fopen(vpath, "r");
    if (!fp) {
        if (errno == ENOENT) return 0;
        perror("[LOAD] visited"); return -1;
    }
    char line[MAX_URL_LEN];
    int loaded = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]='\0';
        if (len == 0) continue;
        visited_insert(v, line);
        loaded++;
    }
    fclose(fp);

    // --- load queue.txt (frontier URLs not yet fetched) ---
    FILE *qp = fopen(qpath, "r");
    if (qp) {
        while (fgets(line, sizeof(line), qp)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]='\0';
            if (len == 0) continue;
            // only re-enqueue if not already visited
            if (!visited_contains(v, line))
                queue_push(q, line);
        }
        fclose(qp);
    }

    return loaded;
}
