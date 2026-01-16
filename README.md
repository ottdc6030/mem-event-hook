# mem-event-hook (LD_PRELOAD event logger)

A small Linux systems project that demonstrates **shared library interposition** (via `LD_PRELOAD`) to observe what a process does at runtime.

## Brief summary

This project is a lightweight runtime tracing tool for Linux:

- Injects a shared library with `LD_PRELOAD` to intercept selected libc calls (allocations, threads, `mmap`/`munmap`, `fork`/`vfork`, and `clone3`).
- Records timestamped events per thread and writes per-process CSV traces (`logs/<pid>/*.csv`) for performance investigation (allocation churn, thread activity, VM mapping behavior).
- Uses thread-local state and an async flushing thread to reduce self-observation and keep overhead low.

When the shared library is injected into a target program, it **overrides common libc functions** (memory allocation, thread creation, mapping, etc.), records each call as an event, and writes the results to **per-process CSV logs**.

This is the kind of technique used in profilers, debuggers, sandboxes, and observability tooling — and it touches multiple OS concepts: ELF dynamic linking, process/thread lifecycle, `fork`/`exec`, syscalls, thread-local state, and concurrency.

---

## What it hooks

When preloaded, the library intercepts these APIs and logs them:

- Memory: `malloc`, `calloc`, `realloc`, `free`
- Virtual memory: `mmap`, `munmap`
- Threads: `pthread_create`, `pthread_exit`
- Process lifecycle: `exit`, `fork`, `vfork`
- Selected libc helpers: `memcpy`, `strncpy`
- Syscall monitoring: `syscall(435)` for `clone3` (Linux)
- Program entry: `__libc_start_main` is wrapped to observe `main()` start/end

Each event includes:

- Thread ID
- Timestamp (nanoseconds, relative to the first observed event)
- Event-specific fields (sizes, pointers, flags, return values, etc.)

---

## Build

From this directory:

```bash
make
```

This produces the shared library:

- `liboverride.so`

Other useful Makefile targets:

- `make test` — builds the demo binary `test`
- `make hi` — builds the tiny helper binary `hi`
- `make run_test` — builds everything and runs `test` with `LD_PRELOAD`

To clean:

```bash
make clean
```

---

## Quick demo (included test program)

The repo includes a small test binary that performs allocations, spawns threads, and uses `mmap`.

```bash
make run_test
```

This target:

- Builds `liboverride.so`
- Builds `test` and `hi`
- Runs `test` with the library injected via `LD_PRELOAD`

---

## Use on any program

You can inject the hook library into (most) dynamically-linked Linux binaries:

```bash
mkdir -p logs
LD_PRELOAD=$PWD/liboverride.so \
  LD_PRELOAD_LOG=$PWD/logs \
  /path/to/your/program --args
```

If `LD_PRELOAD_LOG` is not set, the library defaults to writing under `./logs/`.

### Where logs go

Logs are written under:

- `logs/<pid>/*.csv`

Example (names vary by what the program actually calls):

- `logs/12345/malloc.csv`
- `logs/12345/free.csv`
- `logs/12345/thread_create.csv`
- `logs/12345/mmap.csv`

The CSV header row describes the columns for each event type.

---

## How it works (high level)

### 1) Symbol interposition (`LD_PRELOAD`)

On Linux, the dynamic loader resolves function symbols at runtime. If a preloaded shared object exports a symbol with the same name as a libc function (for example `malloc`), the loader will resolve calls to **your** version first.

To still call the real implementation, the project uses:

- `dlsym(RTLD_NEXT, "malloc")` to find the “next” definition in the resolution chain

### 2) Avoiding recursion / self-observation

Hooks often call other libc functions internally (logging, allocating buffers, etc.). If you naively log everything, the logger logs itself and spirals.

This project uses a **thread-local flag** to temporarily disable “new behavior” while it is inside an override, preventing the hook’s own internal work from polluting the trace.

### 3) Low-overhead event queue + background flushing

Each override quickly packages a small event payload and enqueues it. A background worker thread flushes batches of events to CSV files.

A constructor (`__attribute__((constructor))`) sets up the queue and worker thread when the library loads; a destructor flushes/cleans up when the process exits.

### 4) Process/thread lifecycle edge cases

- `fork`/`vfork` have special handling so the worker thread doesn’t get duplicated incorrectly
- `pthread_atfork` is used to stop/restart the worker around forks
- `__libc_start_main` is wrapped so the tool can bracket the program’s lifetime

---

## Notes / limitations

- Linux/glibc-focused: relies on `LD_PRELOAD` and glibc startup conventions.
- `LD_PRELOAD` is ignored for setuid/setgid binaries for security reasons.
- The logs capture **calls**, not semantic intent — interpreting them is a separate layer.

---

## Why this belongs on an OS-focused portfolio

This repo demonstrates practical experience with:

- ELF dynamic linking and runtime symbol resolution
- ABI-safe function interposition and calling the “real” implementation
- Thread-local storage (TLS) to control hook behavior per thread
- Synchronization primitives (`mutex`/`condvar`) and background worker design
- Interactions between `fork`/`exec` and multithreaded programs
- Translating low-level runtime activity into structured data (CSV traces)

---

## Repository layout

- `define_override.c/.h` — hook definitions and interposition macros
- `event_queue.c/.h` — event buffering and CSV log writer
- `alloc_map.cpp/.h` — simple per-thread pointer/event history tracking
- `test.c`, `hi.c` — demo programs
- `Makefile` — build + `run_test` target
