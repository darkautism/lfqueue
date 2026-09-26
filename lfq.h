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

struct lfq_producer_hp;

struct lfq_ctx {
    struct lfq_node * volatile head;
    struct lfq_node * volatile tail;

    struct lfq_node * volatile retired_head;
    lfq_atomic_int_t retired_count;
    lfq_atomic_int_t reclaiming;

    /* Dynamically-grown producer hazard records; records live until clean(). */
    struct lfq_producer_hp * volatile producer_hps;

    struct lfq_node * volatile *HP;
    lfq_atomic_int_t *tid_map;
    int MAXHPSIZE;

    /* One atomic gate: closing bit + active operation count. */
    lfq_atomic_int_t op_state;
};

#ifdef LFQ_TEST_HOOKS
enum lfq_test_hook_point {
    LFQ_TEST_ENQ_AFTER_PROTECT_TAIL = 1,
    LFQ_TEST_ENQ_AFTER_LINK,
    LFQ_TEST_DEQ_AFTER_PROTECT_HEAD,
    LFQ_TEST_DEQ_AFTER_PROTECT_NEXT,
    LFQ_TEST_DEQ_AFTER_HEAD_CAS,
    LFQ_TEST_RECLAIM_AFTER_DETACH,
    LFQ_TEST_CLEAN_AFTER_GATE
};

typedef void (*lfq_test_hook_fn)(enum lfq_test_hook_point point,
                                 struct lfq_ctx *ctx,
                                 struct lfq_node *first,
                                 struct lfq_node *second,
                                 void *arg);

void lfq_test_set_hook(lfq_test_hook_fn hook, void *arg);
void lfq_test_force_reclaim(struct lfq_ctx *ctx);
#endif

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
