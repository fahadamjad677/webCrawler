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
│   ├── main.c             # Entry point, init, path setup, signal handling
│   ├── queue.c            # Circular bounded URL queue
│   ├── visited.c          # Hash-table visited set (O(1) lookup)
│   ├── fetcher.c          # libcurl HTTP fetcher
│   ├── parser.c           # HTML link extractor + URL resolver
│   ├── persistence.c      # Save/load visited.txt (resume)
│   ├── logger.c           # Thread-safe timestamped logger
│   ├── worker.c           # Worker thread logic
│   └── stats.c            # Final statistics table
├── obj/                    # Auto-created: compiled object files
├── data/
│   └── <site-name>/        # Auto-created: one folder per crawled site
│       ├── visited.txt    # Persisted visited URLs (resume)
│       └── crawler.log    # Full run log
└── docs/
    └── README.md
```

Each crawl run derives a folder name from the seed URL's host (e.g.
`https://example.com` → `data/example.com/`). All progress and logs for that
site are stored under its own folder, so multiple sites can be crawled and
resumed independently.

---

## Setup (Ubuntu WSL)

```bash
# Install dependencies
sudo apt update
sudo apt install -y libcurl4-openssl-dev gcc make

# Build
cd webcrawler
make

#If Data already Exists then 
make clean
make

# Run with default seed URL
./crawler

# Run with a custom seed URL (also sets the data/ subfolder name)
./crawler https://example.com

# Resume a previous crawl for the same site (just re-run with the same seed)
./crawler https://example.com
```
# some Test Cases

./crawler https://httpbin.org
./crawler https://www.iana.org/help/example-domains
./crawler https://neverssl.com
./crawler https://www.geekybugs.com/

> Note: the first argument's host determines the data folder, e.g.
> `data/example.com/visited.txt` and `data/example.com/crawler.log`.

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
   URLs to `data/<site-name>/visited.txt` using an atomic `write-to-tmp → rename`.
2. On startup, `load_visited()` reads that file back into the hash table.
3. Seed URLs that are already in the visited set are skipped.
4. So restarting the crawler with the same seed picks up exactly where it left off.
5. Press **Ctrl+C** at any time — the signal handler saves state before exit.

---

## Sample Output

```
[SYSTEM] Domain filter : https://example.com
[SYSTEM] Data dir      : data/example.com
[SYSTEM] Page cap      : 500 pages

[SYSTEM] No resume file found — starting fresh crawl
[SYSTEM] Starting 4 worker threads

[10:23:01][Thread A] Fetching  --> https://example.com  [queue: 1]
[10:23:01][Thread B] Fetching  --> https://www.iana.org  [queue: 0]
[10:23:02][Thread A] Done      <-- https://example.com  [3 links found]
[10:23:02][Thread A] Queued    +++ 2 new links
[10:23:02][Thread A] [SAVING] Progress saved to 'data/example.com/visited.txt'  (13 visited)
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
  Progress saved to: data/example.com/visited.txt
```

---

## Build Artifacts

- Compiled `.o` files go into `obj/`.
- `make clean` removes `obj/`, the `crawler` binary, and the entire `data/`
  directory (all per-site crawl progress and logs).

