
# CS525 – Assignment 2: Buffer Manager

**Authors:** Harsh Kanakhara (A20642254), Kevin Mevada (A20639598)
**Date:** September 20, 2025

This project is a buffer manager that supports:

* **FIFO**
* **LRU**
* **CLOCK** (extra credit)
* **LRU-K** (handled as LRU)

All public functions are **thread-safe**.

---

## How to Build and Run

1. Add your `storage_mgr.c` from Assignment 1 to this folder.
2. Run:

```bash
make
./test_assign2_1
./test_assign2_2
```

---

## Key Features

* **Eviction:** Only unpinned pages are replaced.
* **Dirty Pages:** Written to disk before reuse.
* **I/O Counters:** Track successful reads and writes.
* **Thread Safety:** Uses a global mutex for all public functions.

---

## Strategies

* **FIFO:** First-in, first-out.
* **LRU:** Least recently used.
* **CLOCK:** Second-chance algorithm.
* **LRU-K:** Treated as LRU for testing.

---

## Tested With

* `test_assign2_1.c`
* `test_assign2_2.c`

---

## Files

* `buffer_mgr.c` — Main code
* `storage_mgr.c` — From Assignment 1 (add this)
* `buffer_mgr.h`, `buffer_mgr_stat.c/h`, `dberror.c/h`, `dt.h` — Provided
* `test_assign2_1.c`, `test_assign2_2.c`, `test_helper.h` — Tests
* `Makefile` — For compiling and running

---

Let me know if you'd like this in a `.md` or `.txt` format!
