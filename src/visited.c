#include "crawler.h"

/* djb2 hash */
static unsigned int hash_url(const char *url) {
    unsigned int h = 5381;
    int c;
    while ((c = (unsigned char)*url++))
        h = ((h << 5) + h) + c;
    return h & (VISITED_CAPACITY - 1); /* mask: capacity is power of 2 */
}

void visited_init(VisitedSet *v) {
    memset(v, 0, sizeof(*v));
}

int visited_contains(const VisitedSet *v, const char *url) {
    if (v->count == 0) return 0;

    unsigned int idx = hash_url(url);
    for (int i = 0; i < VISITED_CAPACITY; i++) {
        unsigned int probe = (idx + i) & (VISITED_CAPACITY - 1);
        if (!v->occupied[probe])  return 0;           /* empty slot = not found */
        if (strcmp(v->table[probe], url) == 0) return 1;
    }
    return 0;
}

/* Returns 1 if inserted, 0 if already present or table full */
int visited_insert(VisitedSet *v, const char *url) {
    if (v->count >= VISITED_CAPACITY * 3 / 4) return 0; /* 75% load limit */

    unsigned int idx = hash_url(url);
    for (int i = 0; i < VISITED_CAPACITY; i++) {
        unsigned int probe = (idx + i) & (VISITED_CAPACITY - 1);
        if (!v->occupied[probe]) {
            strncpy(v->table[probe], url, MAX_URL_LEN - 1);
            v->table[probe][MAX_URL_LEN - 1] = '\0';
            v->occupied[probe] = 1;
            v->count++;
            return 1;
        }
        if (strcmp(v->table[probe], url) == 0) return 0; /* already present */
    }
    return 0;
}

int visited_count(const VisitedSet *v) {
    return v->count;
}
