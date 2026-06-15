# Multi-threaded Web Crawler with Resume Capability
**OS Lab Project 1 — Spring 2026**

---

## Project Structure

```
webcrawler/
├── Makefile
├── include/
│   └── crawler.h          # All types, constants, prototypes
├── src/
│   ├── main.c             # Entry point, init, signal handling
│   ├── queue.c            # Circular bounded URL queue
│   ├── visited.c          # Hash-table visited set (O(1) lookup)
│   ├── fetcher.c          # libcurl HTTP fetcher
│   ├── parser.c           # HTML link extractor + URL resolver
│   ├── persistence.c      # Save/load visited.txt (resume)
│   ├── logger.c           # Thread-safe timestamped logger
│   ├── worker.c           # Worker thread logic
│   └── stats.c            # Final statistics table
├── data/
│   ├── visited.txt        # Auto-created: persisted visited URLs
│   └── crawler.log        # Auto-created: full run log
└── docs/
    └── README.md
```

---

## Setup (Ubuntu WSL)

```bash
# Install dependencies
sudo apt update
sudo apt install -y libcurl4-openssl-dev gcc make

# Build
cd webcrawler
make

# Run with default seed URLs
./crawler

# Run with custom seed URLs
./crawler https://example.com https://httpbin.org

# Resume a previous crawl (just re-run — it loads data/visited.txt automatically)
./crawler
```

---

## OS Concepts Demonstrated

| Concept                   | Where Used                                   |
|---------------------------|----------------------------------------------|
| POSIX Threads (`pthread`) | `worker.c` — `THREAD_COUNT` worker threads   |
| Mutex (`pthread_mutex_t`) | Protects shared queue + visited set in all writes |
| Condition Variables (`pthread_cond_t`) | `work_available` — threads sleep when queue empty, wake on new links |
| Thread Synchronization    | Shutdown detection: queue empty + active_threads == 0 |
| File I/O Persistence      | `persistence.c` — atomic write via tmp+rename |
| Signal Handling           | SIGINT (Ctrl+C) triggers graceful save+shutdown |
| Dynamic Memory            | `realloc` in fetcher for response buffer     |

---

## How the Resume Works

1. On every successful page fetch, `save_visited()` writes all visited
   URLs to `data/visited.txt` using an atomic `write-to-tmp → rename`.
2. On startup, `load_visited()` reads that file back into the hash table.
3. Seed URLs that are already in the visited set are skipped.
4. So restarting the crawler picks up exactly where it left off.
5. Press **Ctrl+C** at any time — the signal handler saves state before exit.

---

## Sample Output

```
[SYSTEM] Loading 'data/visited.txt'... 12 URLs found
[SYSTEM] Starting 4 worker threads

[10:23:01][Thread A] Fetching  --> https://example.com  [queue: 1]
[10:23:01][Thread B] Fetching  --> https://www.iana.org  [queue: 0]
[10:23:02][Thread A] Done      <-- https://example.com  [3 links found]
[10:23:02][Thread A] Queued    +++ 2 new links
[10:23:02][Thread A] [SAVING] Progress saved to 'data/visited.txt'  (13 visited)
[10:23:03][Thread B] Done      <-- https://www.iana.org  [8 links found]
...

╔══════════════════════════════════════════════════╗
║          CRAWL COMPLETE — FINAL STATISTICS       ║
╠══════════╦══════════════╦═════════════╦══════════╣
║  Thread  ║ Pages Fetched║ Links Found ║  Errors  ║
╠══════════╬══════════════╬═════════════╬══════════╣
║  Thread A ║            6 ║          18 ║        0 ║
║  Thread B ║            5 ║          15 ║        1 ║
║  Thread C ║            4 ║          12 ║        0 ║
║  Thread D ║            3 ║           9 ║        0 ║
╠══════════╬══════════════╬═════════════╬══════════╣
║  TOTAL   ║           18 ║          54 ║        1 ║
╚══════════╩══════════════╩═════════════╩══════════╝

  Total unique URLs visited: 18
  Progress saved to: data/visited.txt
```
