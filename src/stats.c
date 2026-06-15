#include "crawler.h"

void print_stats(const CrawlerState *cs) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║          CRAWL COMPLETE — FINAL STATISTICS       ║\n");
    printf("╠══════════╦══════════════╦═════════════╦══════════╣\n");
    printf("║  Thread  ║ Pages Fetched║ Links Found ║  Errors  ║\n");
    printf("╠══════════╬══════════════╬═════════════╬══════════╣\n");

    const char *labels[] = {"A","B","C","D","E","F","G","H"};
    long total_pages = 0, total_links = 0, total_errors = 0;

    for (int i = 0; i < THREAD_COUNT; i++) {
        printf("║  Thread %s ║ %12ld ║ %11ld ║ %8ld ║\n",
               labels[i],
               cs->stats[i].pages_fetched,
               cs->stats[i].links_found,
               cs->stats[i].errors);
        total_pages  += cs->stats[i].pages_fetched;
        total_links  += cs->stats[i].links_found;
        total_errors += cs->stats[i].errors;
    }

    printf("╠══════════╬══════════════╬═════════════╬══════════╣\n");
    printf("║  TOTAL   ║ %12ld ║ %11ld ║ %8ld ║\n",
           total_pages, total_links, total_errors);
    printf("╚══════════╩══════════════╩═════════════╩══════════╝\n");
    printf("\n  Total unique URLs visited: %d\n",
           visited_count(&cs->visited));
    printf("  Progress saved to: %s\n\n", SAVE_FILE);
}
