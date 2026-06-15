#include "crawler.h"

void queue_init(URLQueue *q) {
    memset(q, 0, sizeof(*q));
    q->head = 0;
    q->tail = 0;
    q->size = 0;
}

/* Returns 1 on success, 0 if queue is full */
int queue_push(URLQueue *q, const char *url) {
    if (q->size >= QUEUE_CAPACITY) return 0;
    strncpy(q->items[q->tail], url, MAX_URL_LEN - 1);
    q->items[q->tail][MAX_URL_LEN - 1] = '\0';
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->size++;
    return 1;
}

/* Returns 1 and copies URL into out on success, 0 if empty */
int queue_pop(URLQueue *q, char *out) {
    if (q->size == 0) return 0;
    strncpy(out, q->items[q->head], MAX_URL_LEN - 1);
    out[MAX_URL_LEN - 1] = '\0';
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->size--;
    return 1;
}

int queue_size(const URLQueue *q) {
    return q->size;
}

int queue_is_empty(const URLQueue *q) {
    return q->size == 0;
}
