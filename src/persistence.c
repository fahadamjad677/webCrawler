#include "crawler.h"
#include <sys/stat.h>
#include <errno.h>

/*
 * save_visited: write all visited URLs to `path`, one per line.
 * Called periodically (under the mutex) to persist crawler state.
 *
 * Uses a temp file + rename for atomic write — no half-written file.
 */

int   save_visited(const VisitedSet *v, const char *path);
int   load_visited(VisitedSet *v, URLQueue *q, const char *path);
void  build_data_paths(CrawlerState *cs, const char *seed_url);


int save_visited(const VisitedSet *v, const char *path) {
    /* write to a temp file first */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        perror("[SAVE] fopen tmp");
        return -1;
    }

    for (int i = 0; i < VISITED_CAPACITY; i++) {
        if (v->occupied[i]) {
            fprintf(fp, "%s\n", v->table[i]);
        }
    }

    fflush(fp);
    fclose(fp);

    /* atomic rename */
    if (rename(tmp_path, path) != 0) {
        perror("[SAVE] rename");
        return -1;
    }

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
int load_visited(VisitedSet *v, URLQueue *q, const char *path) {
    (void)q; /* queue is not re-populated from visited — already crawled */

    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) return 0; /* first run, no file yet */
        perror("[LOAD] stat");
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("[LOAD] fopen");
        return -1;
    }

    char line[MAX_URL_LEN];
    int  loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        visited_insert(v, line);
        loaded++;
    }

    fclose(fp);
    return loaded;
}
