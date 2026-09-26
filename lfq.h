#ifndef __LFQ_H__
#define __LFQ_H__

#include "cross-platform.h"

#define LFQ_DEFAULT_MAX_CONSUMERS 16
#define LFQ_HAZARDS_PER_THREAD 2
#define LFQ_ERROR ((void *)-1)

struct lfq_node {
    void *data;
    struct lfq_node * volatile next;
    struct lfq_node *retired_next;
};

struct lfq_ctx {
    LFQ_ALIGNAS(64) struct lfq_node * volatile head;
    LFQ_ALIGNAS(64) struct lfq_node * volatile tail;

    struct lfq_node * volatile retired_head;
    lfq_atomic_int_t retired_count;
    lfq_atomic_int_t reclaiming;
    /* Prevent reclamation while a producer may hold a stale local tail. */
    lfq_atomic_int_t active_enqueues;

    struct lfq_node * volatile *HP;
    lfq_atomic_int_t *tid_map;
    int MAXHPSIZE;

    /* One atomic gate: closing bit + active operation count. */
    lfq_atomic_int_t op_state;
};

int lfq_init(struct lfq_ctx *ctx, int max_consume_thread);
int lfq_clean(struct lfq_ctx *ctx);

/* Compatibility name: now returns the number of retired, not-yet-reclaimed nodes. */
long lfg_count_freelist(const struct lfq_ctx *ctx);

int lfq_enqueue(struct lfq_ctx *ctx, void *data);

/*
 * Status-returning dequeue API:
 *   1  item dequeued and stored in *out
 *   0  queue empty
 *  <0  negative errno-style error
 */
int lfq_try_dequeue(struct lfq_ctx *ctx, void **out);
int lfq_try_dequeue_tid(struct lfq_ctx *ctx, int tid, void **out);

/*
 * Legacy pointer-returning API:
 *   NULL      queue empty
 *   LFQ_ERROR error
 *   otherwise dequeued payload
 */
void *lfq_dequeue(struct lfq_ctx *ctx);
void *lfq_dequeue_tid(struct lfq_ctx *ctx, int tid);

#define LFQ_MB_DEQUEUE(ctx, ret) do { \
    do { \
        (ret) = lfq_dequeue((ctx)); \
        mb(); \
    } while ((ret) == NULL); \
} while (0)

#endif
