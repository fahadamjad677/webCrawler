#include "crawler.h"
#include <stdarg.h>
#include <time.h>

static const char *thread_label(int id) {
    static const char *labels[] = {"A", "B", "C", "D", "E", "F", "G", "H"};
    if (id >= 0 && id < (int)(sizeof(labels)/sizeof(labels[0])))
        return labels[id];
    return "?";
}

/*
 * log_msg: printf-style logger that:
 *   - prefixes each line with timestamp + thread label
 *   - writes to stdout and to the log file
 *   - is called while holding the global mutex (thread_id == -1 for
 *     system messages, >= 0 for worker messages)
 */
void log_msg(CrawlerState *cs, int thread_id, const char *fmt, ...) {
    /* timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[20];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);

    char prefix[32];
    if (thread_id < 0)
        snprintf(prefix, sizeof(prefix), "[%s][SYSTEM  ]", ts);
    else
        snprintf(prefix, sizeof(prefix), "[%s][Thread %s]", ts,
                 thread_label(thread_id));

    va_list args;

    /* stdout */
    va_start(args, fmt);
    printf("%s ", prefix);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);

    /* log file */
    if (cs->logfp) {
        va_start(args, fmt);
        fprintf(cs->logfp, "%s ", prefix);
        vfprintf(cs->logfp, fmt, args);
        fprintf(cs->logfp, "\n");
        fflush(cs->logfp);
        va_end(args);
    }
}
