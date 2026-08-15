# c_database_engine

A minimal, educational database engine written from scratch in C. It implements a simple REPL (`db >`) that accepts a small set of commands, stores rows in fixed-size pages, and persists data to disk between runs.

This project is inspired by the classic "build your own SQLite clone in C" style of learning exercise, and is meant as a hands-on way to understand how a database engine handles memory, paging, and file I/O under the hood.

## Features

- **Interactive CLI (REPL)** — `db >` prompt that reads and executes commands in a loop
- **Meta-commands** — commands prefixed with `.`, e.g. `.exit`
- **Basic statements**
  - `insert <id> <username> <email>` — insert a row
  - `select` — print all rows in the table
- **Fixed-schema row storage** — each row has an `id` (uint32), a `username` (up to 32 chars), and an `email` (up to 255 chars)
- **Paged storage engine**
  - Rows are serialized into fixed-size 4KB pages (`PAGE_SIZE`)
  - Up to 100 pages per table (`TABLE_MAX_PAGES`), ~13 rows per page
- **Pager with disk persistence** — a `Pager` struct manages reading pages from and flushing pages to a database file, so data survives restarts
- **Cursor abstraction** — `Cursor` walks over rows in the table for iteration (used by `select`)

## Project structure

```
.
├── main.c        # Entire engine: pager, table, cursor, REPL, and command handling
├── test.sh        # Test script
└── .gitignore
```

## Building

You'll need `gcc` (or any standard C compiler) and a POSIX-compatible environment (uses `unistd.h`, `fcntl.h`, etc.).

```bash
gcc -o db main.c
```

## Usage

The engine takes a database filename as its only argument. If the file doesn't exist, it will be created.

```bash
./db mydatabase.db
```

Example session:

```
db > insert 1 alice alice@example.com
db > insert 2 bob bob@example.com
db > select
(1, alice, alice@example.com)
(2, bob, bob@example.com)
db > .exit
```

Running the tests:

```bash
./test.sh
```

## How it works

- **Rows** are packed into a fixed byte layout (`ID_SIZE` + `USERNAME_SIZE` + `EMAIL_SIZE`) via `serialize_row` / `deserialize_row`.
- **Pages** hold a fixed number of rows (`ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE`). The `Table` struct keeps an in-memory array of page pointers.
- **The Pager** lazily loads pages from disk into memory on first access (`get_page`) and flushes dirty pages back to disk on `db_close`.
- **The REPL loop** in `main()` reads a line of input, classifies it as a meta-command (starts with `.`) or a statement, then prepares and executes it.

## Roadmap

- [ ] **B-Tree based indexing** — replace the current flat/paged row storage with a B-Tree structure to support efficient lookups, ordered iteration, and scalable inserts (this is the same core data structure used by real engines like SQLite). This is planned as the next major addition to the project.
- [ ] `update` and `delete` statements
- [ ] `WHERE`-style filtering on `select`
- [ ] Variable-length pages / multiple tables

## Disclaimer

This is a learning project, not production database software. It's a great way to explore concepts like manual memory management, binary serialization, and file-backed storage in C — but it should not be used to store data you care about.
