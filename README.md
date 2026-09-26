# lfqueue

[![C/C++ CI](https://github.com/darkautism/lfqueue/actions/workflows/ci.yml/badge.svg?branch=HP)](https://github.com/darkautism/lfqueue/actions/workflows/ci.yml)

A small multi-producer / multi-consumer lock-free queue written in C.

The default branch is the Hazard Pointer implementation.  The code is intentionally
kept compact enough to study, but concurrent memory reclamation is subtle: run the
sanitizer tests before using changes in real systems.

## Current design

The queue uses the Michael-Scott linked-queue protocol:

- enqueue linearizes when it CAS-links the new node into `tail->next`;
- lagging `tail` pointers are helped forward by producers and consumers;
- dequeue protects both the current dummy head and its successor with two Hazard
  Pointers before dereferencing them;
- removed dummy nodes go to a separate retired list.  The retired-list link is
  **not** overlaid on the queue's `next` field, so a hazard-protected node remains
  immutable until no reader can reference it;
- all shared queue pointers, hazard slots, thread-slot ownership, and lifecycle
  state are accessed through full atomic operations on GCC/Clang and MSVC.

`lfq_clean()` uses an atomic lifecycle gate.  It succeeds only while no queue
operation is active; once cleanup starts, new operations fail with `-EBUSY`
instead of racing with reclamation.

## Build and test

GNU/Clang:

```sh
make
make test
make test-sanitize
make test-tsan
```

CI runs GCC, Clang, AddressSanitizer + UndefinedBehaviorSanitizer,
ThreadSanitizer, and MSVC tests.

Visual Studio projects are also included under `Visual Stdio/`.

## Basic example

```c
#include <stdint.h>
#include <stdio.h>
#include "lfq.h"

int main(void) {
    struct lfq_ctx ctx;

    if (lfq_init(&ctx, 0) != 0)
        return 1;                       /* 0 => default 16 consumer slots */

    lfq_enqueue(&ctx, (void *)(uintptr_t)1);
    lfq_enqueue(&ctx, (void *)(uintptr_t)2);

    for (;;) {
        void *p = lfq_dequeue(&ctx);
        if (p == NULL)
            break;                      /* empty */
        if (p == LFQ_ERROR)
            return 1;                   /* errno-style failure */
        printf("%lu\n", (unsigned long)(uintptr_t)p);
    }

    return lfq_clean(&ctx) == 0 ? 0 : 1;
}
```

## API

### `lfq_init(ctx, max_consume_thread)`

Initializes a fresh context.  Passing `0` selects
`LFQ_DEFAULT_MAX_CONSUMERS` (16).  A negative value is rejected.

The caller must not call `lfq_init()` on a live queue; clean it first.

### `lfq_enqueue(ctx, data)`

Enqueues one payload pointer.

`NULL` and `LFQ_ERROR` are rejected with `-EINVAL` because the legacy
pointer-returning dequeue API reserves those values for empty/error results.

### `lfq_try_dequeue(ctx, &out)`

Recommended dequeue API:

- `1`: item returned in `out`
- `0`: queue empty
- negative value: errno-style error

### `lfq_try_dequeue_tid(ctx, tid, &out)`

Same operation with an explicit consumer slot.  `tid` must be in
`[0, max_consume_thread)`.

The implementation reserves the slot atomically for each call, so manual and
automatic dequeue APIs cannot silently use the same Hazard Pointer slots at the
same time.  Concurrent calls using the same explicit `tid` return `-EBUSY`.

### `lfq_dequeue()` / `lfq_dequeue_tid()`

Compatibility wrappers:

- payload pointer on success
- `NULL` when empty
- `LFQ_ERROR` on error

Prefer the status-returning APIs in new code.

### `lfq_clean(ctx)`

Reclaims queue nodes and retired nodes.  User payloads are never freed by the
queue.

Cleanup may be attempted concurrently, but it returns `-EBUSY` if an operation
is already active.  Once cleanup wins the lifecycle gate, new operations are
blocked until cleanup finishes.

## Correctness notes

Older revisions had several important races:

- enqueue published `tail` before linking `old_tail->next`, allowing a
  completed enqueue to remain invisible to consumers;
- the retired-list pointer shared a union with the queue `next` pointer, so a
  node could be modified while another consumer still hazard-protected it;
- Hazard Pointer slots were plain `volatile` loads/stores rather than atomic
  accesses;
- Win64 used an 8-byte CAS on 32-bit `int` fields;
- `lfq_init(ctx, 0)` contradicted the documented default and produced zero
  consumer slots;
- cleanup could leave non-empty queues partially unreclaimed.

The current implementation removes those mechanisms instead of adding more
fences around them.

## Tests

`test_edge_cases.c`
covers API errors, default initialization, FIFO behavior, slot conflicts,
cleanup of non-empty queues, and post-clean behavior.

`test_multithread.c`
runs MPMC producer/consumer matrices and verifies both item count and a
cross-thread checksum.

`test_aba.c`
stresses rapid node retirement/reuse.  It is paired with sanitizer CI; a passing
stress test by itself is not a formal proof of linearizability.

## License

WTFPL
