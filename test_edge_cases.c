#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "lfq.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void) {
    struct lfq_ctx ctx;
    void *out = NULL;

    CHECK(lfq_init(&ctx, 0) == 0);
    CHECK(ctx.MAXHPSIZE == LFQ_DEFAULT_MAX_CONSUMERS);

    CHECK(lfq_enqueue(&ctx, NULL) == -EINVAL);
    CHECK(lfq_enqueue(&ctx, LFQ_ERROR) == -EINVAL);
    CHECK(lfq_try_dequeue_tid(&ctx, -1, &out) == -EINVAL);
    CHECK(lfq_try_dequeue_tid(&ctx, ctx.MAXHPSIZE, &out) == -EINVAL);

    /* Manual and automatic consumers cannot silently share a hazard slot. */
    lfq_atomic_store_int(&ctx.tid_map[1], 1);
    CHECK(lfq_try_dequeue_tid(&ctx, 1, &out) == -EBUSY);
    CHECK(lfq_clean(&ctx) == -EBUSY);
    lfq_atomic_store_int(&ctx.tid_map[1], 0);

    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)1) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)2) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)3) == 0);

    CHECK(lfq_try_dequeue(&ctx, &out) == 1);
    CHECK((uintptr_t)out == 1);
    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 1);
    CHECK((uintptr_t)out == 2);
    CHECK((uintptr_t)lfq_dequeue(&ctx) == 3);
    CHECK(lfq_dequeue(&ctx) == NULL);

    CHECK(lfq_clean(&ctx) == 0);

    /* Cleaning a queue with pending items must reclaim queue nodes cleanly. */
    CHECK(lfq_init(&ctx, 2) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)11) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)12) == 0);
    CHECK(lfq_clean(&ctx) == 0);

    /* Repeated clean is harmless, and operations after clean fail cleanly. */
    CHECK(lfq_clean(&ctx) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)1) == -EINVAL);
    CHECK(lfq_try_dequeue(&ctx, &out) == -EINVAL);

    printf("edge cases PASS\n");
    return 0;
}
