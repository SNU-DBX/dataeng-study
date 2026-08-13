---
icon: lucide/database
---

# Data Engineering — Study Guides

한국어 가이드입니다.

Source-code study guides for a **first, rigorous course in database systems**. The course is a
semester-long arc of programming assignments that distill a real research system into staged,
self-contained features, built on **PostgreSQL 18.4** (OLTP) and **DuckDB** (OLAP).

Each guide is a *targeted tour* of exactly the code an assignment touches — the parts you will
delete, the parts you will bend, and the parts that will break if you get the first two wrong.

## Guides

<div class="grid cards" markdown>

-   **[PA2a — Inside the PostgreSQL Buffer Manager](pa2a/index.md)**

    ---

    Where the buffer pool is born, how a page gets into it, how PostgreSQL decides which page to
    throw out, and how pages get back to disk. Preparation for replacing the single LRU-managed
    pool with *N* generational, sealed pools.

    *~90 min + gdb*

</div>

## The assignment arc

| | Assignment | Theme |
|---|---|---|
| **PA0** | Bootcamp | C/C++ and systems-programming ramp — toolchain, bytes, locks, file I/O |
| **PA1** | DuckDB reads PostgreSQL storage directly | Storage layout, slotted pages, tuple deform |
| **PA2a** | Multiple buffer pools + generational sealing | Buffer manager init, no-evict allocation, the seal |
| **PA2b** | Async flush worker + Copy-on-Seal | Background worker lifecycle, the immutable-but-resident window |
| **PA3** | Cross-engine snapshot isolation *(capstone)* | Snapshot export at the seal, MVCC across engines |

Only the PA2a guide is published so far.
