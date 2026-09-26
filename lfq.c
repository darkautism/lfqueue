#include "cross-platform.h"
#include "lfq.h"

#include <errno.h>
#include <stddef.h>

#define LFQ_RECLAIM_MIN 32
#define LFQ_OP_CLOSING 0x40000000

struct lfq_producer_hp {
    struct lfq_node * volatile hazard;
    lfq_atomic_int_t in_use;
    struct lfq_producer_hp *next;
};

static struct lfq_node *atomic_load_node(struct lfq_node * volatile *ptr) {
    return (struct lfq_node *)lfq_atomic_load_ptr((void * volatile *)ptr);
}

static void atomic_store_node(struct lfq_node * volatile *ptr, struct lfq_node *value) {
    lfq_atomic_store_ptr((void * volatile *)ptr, value);
}

static struct lfq_node *atomic_exchange_node(struct lfq_node * volatile *ptr,
                                             struct lfq_node *value) {
    return (struct lfq_node *)lfq_atomic_exchange_ptr((void * volatile *)ptr, value);
}

static bool atomic_cas_node(struct lfq_node * volatile *ptr,
                            struct lfq_node **expected,
                            struct lfq_node *desired) {
    void *raw_expected = *expected;
    bool ok = lfq_atomic_cas_ptr((void * volatile *)ptr, &raw_expected, desired);
    *expected = (struct lfq_node *)raw_expected;
    return ok;
}

static struct lfq_producer_hp *producer_hp_acquire(struct lfq_ctx *ctx) {
    struct lfq_producer_hp *record =
        (struct lfq_producer_hp *)lfq_atomic_load_ptr((void * volatile *)&ctx->producer_hps);

    for (; record; record = record->next) {
        int expected = 0;
        if (lfq_atomic_cas_int(&record->in_use, &expected, 1)) {
            lfq_atomic_store_ptr((void * volatile *)&record->hazard, NULL);
            return record;
        }
    }

    record = (struct lfq_producer_hp *)calloc(1, sizeof(*record));
    if (!record)
        return NULL;

    lfq_atomic_store_ptr((void * volatile *)&record->hazard, NULL);
    lfq_atomic_store_int(&record->in_use, 1);

    for (;;) {
        struct lfq_producer_hp *old =
            (struct lfq_producer_hp *)lfq_atomic_load_ptr((void * volatile *)&ctx->producer_hps);
        record->next = old;
        void *expected = old;
        if (lfq_atomic_cas_ptr((void * volatile *)&ctx->producer_hps, &expected, record))
            return record;
    }
}

static void producer_hp_release(struct lfq_producer_hp *record) {
    lfq_atomic_store_ptr((void * volatile *)&record->hazard, NULL);
    lfq_atomic_store_int(&record->in_use, 0);
}

static int begin_op(struct lfq_ctx *ctx) {
    if (!ctx)
        return -EINVAL;

    for (;;) {
        int state = lfq_atomic_load_int(&ctx->op_state);
        if (state & LFQ_OP_CLOSING)
            return -EBUSY;
        if ((state & ~LFQ_OP_CLOSING) == LFQ_OP_CLOSING - 1)
            return -EOVERFLOW;

        int expected = state;
        if (lfq_atomic_cas_int(&ctx->op_state, &expected, state + 1))
            break;
    }

    if (!ctx->HP || !ctx->tid_map || !atomic_load_node(&ctx->head)) {
        (void)lfq_atomic_fetch_add_int(&ctx->op_state, -1);
        return -EINVAL;
    }
    return 0;
}

static void end_op(struct lfq_ctx *ctx) {
    (void)lfq_atomic_fetch_add_int(&ctx->op_state, -1);
}

static size_t hazard_slot_count(const struct lfq_ctx *ctx) {
    return (size_t)ctx->MAXHPSIZE * LFQ_HAZARDS_PER_THREAD;
}

static void hp_store(struct lfq_ctx *ctx, int tid, int slot, struct lfq_node *node) {
    size_t index = (size_t)tid * LFQ_HAZARDS_PER_THREAD + (size_t)slot;
    lfq_atomic_store_ptr((void * volatile *)&ctx->HP[index], node);
}

static struct lfq_node *hp_load(const struct lfq_ctx *ctx, size_t index) {
    return (struct lfq_node *)lfq_atomic_load_ptr((void * volatile *)&ctx->HP[index]);
}

static void hp_clear_pair(struct lfq_ctx *ctx, int tid) {
    hp_store(ctx, tid, 0, NULL);
    hp_store(ctx, tid, 1, NULL);
}

static bool node_is_hazard(const struct lfq_ctx *ctx, const struct lfq_node *node) {
    size_t count = hazard_slot_count(ctx);
    for (size_t i = 0; i < count; ++i) {
        if (hp_load(ctx, i) == node)
            return true;
    }

    struct lfq_producer_hp *record =
        (struct lfq_producer_hp *)lfq_atomic_load_ptr((void * volatile *)&ctx->producer_hps);
    for (; record; record = record->next) {
        if ((struct lfq_node *)lfq_atomic_load_ptr((void * volatile *)&record->hazard) == node)
            return true;
    }

    return false;
}

static void retired_push(struct lfq_ctx *ctx, struct lfq_node *node) {
    /*
     * Count before publication so a concurrent reclaimer never frees a node
     * that has not yet been included in retired_count.
     */
    (void)lfq_atomic_fetch_add_int(&ctx->retired_count, 1);

    struct lfq_node *old;
    do {
        old = atomic_load_node(&ctx->retired_head);
        node->retired_next = old;
    } while (!atomic_cas_node(&ctx->retired_head, &old, node));
}

static void retired_requeue(struct lfq_ctx *ctx, struct lfq_node *node) {
    struct lfq_node *old;
    do {
        old = atomic_load_node(&ctx->retired_head);
        node->retired_next = old;
    } while (!atomic_cas_node(&ctx->retired_head, &old, node));
}

static void try_reclaim(struct lfq_ctx *ctx, bool force) {
    int expected = 0;
    if (!lfq_atomic_cas_int(&ctx->reclaiming, &expected, 1))
        return;

    int threshold = ctx->MAXHPSIZE * LFQ_HAZARDS_PER_THREAD * 2;
    if (threshold < LFQ_RECLAIM_MIN)
        threshold = LFQ_RECLAIM_MIN;

    if (!force && lfq_atomic_load_int(&ctx->retired_count) < threshold) {
        lfq_atomic_store_int(&ctx->reclaiming, 0);
        return;
    }

    /*
     * A long scan can overlap many new retirements.  Drain a few snapshots in
     * one ownership period so the last operations in a burst do not leave a
     * large retired list merely because they lost the reclaimer CAS.
     */
    for (int pass = 0; pass < 4; ++pass) {
        struct lfq_node *list = atomic_exchange_node(&ctx->retired_head, NULL);
        if (!list)
            break;

        int freed = 0;
        while (list) {
            struct lfq_node *next = list->retired_next;
            if (node_is_hazard(ctx, list)) {
                retired_requeue(ctx, list);
            } else {
                free(list);
                ++freed;
            }
            list = next;
        }

        if (freed)
            (void)lfq_atomic_fetch_add_int(&ctx->retired_count, -freed);

        if (!force && lfq_atomic_load_int(&ctx->retired_count) < threshold)
            break;
    }

    lfq_atomic_store_int(&ctx->reclaiming, 0);
}

static int acquire_tid(struct lfq_ctx *ctx, int requested_tid) {
    if (requested_tid >= 0) {
        if (requested_tid >= ctx->MAXHPSIZE)
            return -EINVAL;

        int expected = 0;
        if (!lfq_atomic_cas_int(&ctx->tid_map[requested_tid], &expected, 1))
            return -EBUSY;
        return requested_tid;
    }

    for (int i = 0; i < ctx->MAXHPSIZE; ++i) {
        int expected = 0;
        if (lfq_atomic_cas_int(&ctx->tid_map[i], &expected, 1))
            return i;
    }
    return -EAGAIN;
}

static void release_tid(struct lfq_ctx *ctx, int tid) {
    hp_clear_pair(ctx, tid);
    lfq_atomic_store_int(&ctx->tid_map[tid], 0);
}

static int dequeue_reserved(struct lfq_ctx *ctx, int tid, void **out) {
    for (;;) {
        struct lfq_node *head = atomic_load_node(&ctx->head);
        if (!head)
            return -EINVAL;

        hp_store(ctx, tid, 0, head);
        if (head != atomic_load_node(&ctx->head))
            continue;

        struct lfq_node *tail = atomic_load_node(&ctx->tail);
        struct lfq_node *next = atomic_load_node(&head->next);

        if (!next) {
            hp_clear_pair(ctx, tid);
            *out = NULL;
            return 0;
        }

        hp_store(ctx, tid, 1, next);

        if (head != atomic_load_node(&ctx->head) ||
            next != atomic_load_node(&head->next)) {
            hp_clear_pair(ctx, tid);
            continue;
        }

        if (head == tail) {
            struct lfq_node *expected_tail = tail;
            (void)atomic_cas_node(&ctx->tail, &expected_tail, next);
            hp_clear_pair(ctx, tid);
            continue;
        }

        void *value = next->data;
        struct lfq_node *expected_head = head;
        if (!atomic_cas_node(&ctx->head, &expected_head, next)) {
            hp_clear_pair(ctx, tid);
            continue;
        }

        /*
         * Keep our hazards published while putting the removed dummy on the
         * retired list.  Only after publication is it safe to drop them.
         */
        retired_push(ctx, head);
        hp_clear_pair(ctx, tid);
        try_reclaim(ctx, false);

        *out = value;
        return 1;
    }
}

int lfq_init(struct lfq_ctx *ctx, int max_consume_thread) {
    if (!ctx)
        return -EINVAL;

    if (max_consume_thread == 0)
        max_consume_thread = LFQ_DEFAULT_MAX_CONSUMERS;
    if (max_consume_thread < 0)
        return -EINVAL;

    memset(ctx, 0, sizeof(*ctx));

    struct lfq_node *dummy = (struct lfq_node *)calloc(1, sizeof(*dummy));
    if (!dummy)
        return -ENOMEM;

    size_t hp_count = (size_t)max_consume_thread * LFQ_HAZARDS_PER_THREAD;
    if (hp_count / LFQ_HAZARDS_PER_THREAD != (size_t)max_consume_thread) {
        free(dummy);
        return -EINVAL;
    }

    ctx->HP = (struct lfq_node * volatile *)calloc(hp_count, sizeof(*ctx->HP));
    ctx->tid_map = (lfq_atomic_int_t *)calloc((size_t)max_consume_thread, sizeof(*ctx->tid_map));
    if (!ctx->HP || !ctx->tid_map) {
        free((void *)ctx->HP);
        free((void *)ctx->tid_map);
        free(dummy);
        memset(ctx, 0, sizeof(*ctx));
        return -ENOMEM;
    }

    ctx->MAXHPSIZE = max_consume_thread;
    atomic_store_node(&dummy->next, NULL);
    atomic_store_node(&ctx->head, dummy);
    atomic_store_node(&ctx->tail, dummy);
    atomic_store_node(&ctx->retired_head, NULL);
    lfq_atomic_store_ptr((void * volatile *)&ctx->producer_hps, NULL);
    lfq_atomic_store_int(&ctx->retired_count, 0);
    lfq_atomic_store_int(&ctx->reclaiming, 0);
    lfq_atomic_store_int(&ctx->op_state, 0);

    return 0;
}

long lfg_count_freelist(const struct lfq_ctx *ctx) {
    if (!ctx)
        return 0;
    return (long)lfq_atomic_load_int((lfq_atomic_int_t *)&ctx->retired_count);
}

int lfq_clean(struct lfq_ctx *ctx) {
    if (!ctx)
        return -EINVAL;

    int expected = 0;
    if (!lfq_atomic_cas_int(&ctx->op_state, &expected, LFQ_OP_CLOSING))
        return -EBUSY;

    if (!ctx->HP && !ctx->tid_map && !atomic_load_node(&ctx->head)) {
        memset(ctx, 0, sizeof(*ctx));
        return 0;
    }

    for (int i = 0; i < ctx->MAXHPSIZE; ++i) {
        if (lfq_atomic_load_int(&ctx->tid_map[i]) != 0) {
            lfq_atomic_store_int(&ctx->op_state, 0);
            return -EBUSY;
        }
    }

    struct lfq_node *node = atomic_exchange_node(&ctx->head, NULL);
    atomic_store_node(&ctx->tail, NULL);

    while (node) {
        struct lfq_node *next = atomic_load_node(&node->next);
        free(node);
        node = next;
    }

    node = atomic_exchange_node(&ctx->retired_head, NULL);
    while (node) {
        struct lfq_node *next = node->retired_next;
        free(node);
        node = next;
    }

    struct lfq_producer_hp *producer_hp =
        (struct lfq_producer_hp *)lfq_atomic_exchange_ptr(
            (void * volatile *)&ctx->producer_hps, NULL);
    while (producer_hp) {
        struct lfq_producer_hp *next = producer_hp->next;
        free(producer_hp);
        producer_hp = next;
    }

    free((void *)ctx->HP);
    free((void *)ctx->tid_map);
    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int lfq_enqueue(struct lfq_ctx *ctx, void *data) {
    if (!data || data == LFQ_ERROR)
        return -EINVAL;

    int rc = begin_op(ctx);
    if (rc < 0)
        return rc;

    struct lfq_node *node = (struct lfq_node *)calloc(1, sizeof(*node));
    if (!node) {
        end_op(ctx);
        return -ENOMEM;
    }

    node->data = data;
    node->retired_next = NULL;
    atomic_store_node(&node->next, NULL);

    struct lfq_producer_hp *producer_hp = producer_hp_acquire(ctx);
    if (!producer_hp) {
        free(node);
        end_op(ctx);
        return -ENOMEM;
    }

    for (;;) {
        struct lfq_node *tail = atomic_load_node(&ctx->tail);
        if (!tail) {
            producer_hp_release(producer_hp);
            free(node);
            end_op(ctx);
            return -EINVAL;
        }

        lfq_atomic_store_ptr((void * volatile *)&producer_hp->hazard, tail);
        if (tail != atomic_load_node(&ctx->tail))
            continue;

        struct lfq_node *next = atomic_load_node(&tail->next);
        if (tail != atomic_load_node(&ctx->tail))
            continue;

        if (!next) {
            struct lfq_node *expected_next = NULL;
            if (atomic_cas_node(&tail->next, &expected_next, node)) {
                /*
                 * The link CAS above is the enqueue linearization point.
                 * Advancing tail is only an optimization; other threads help
                 * if this producer is descheduled here.
                 */
                struct lfq_node *expected_tail = tail;
                (void)atomic_cas_node(&ctx->tail, &expected_tail, node);
                producer_hp_release(producer_hp);
                try_reclaim(ctx, false);
                end_op(ctx);
                return 0;
            }
        } else {
            struct lfq_node *expected_tail = tail;
            (void)atomic_cas_node(&ctx->tail, &expected_tail, next);
        }
    }
}

int lfq_try_dequeue_tid(struct lfq_ctx *ctx, int tid, void **out) {
    if (!out || tid < 0)
        return -EINVAL;
    *out = NULL;

    int rc = begin_op(ctx);
    if (rc < 0)
        return rc;

    int acquired = acquire_tid(ctx, tid);
    if (acquired < 0) {
        end_op(ctx);
        return acquired;
    }

    rc = dequeue_reserved(ctx, acquired, out);
    release_tid(ctx, acquired);
    end_op(ctx);
    return rc;
}

int lfq_try_dequeue(struct lfq_ctx *ctx, void **out) {
    if (!out)
        return -EINVAL;
    *out = NULL;

    int rc = begin_op(ctx);
    if (rc < 0)
        return rc;

    int tid = acquire_tid(ctx, -1);
    if (tid < 0) {
        end_op(ctx);
        return tid;
    }

    rc = dequeue_reserved(ctx, tid, out);
    release_tid(ctx, tid);
    end_op(ctx);
    return rc;
}

void *lfq_dequeue_tid(struct lfq_ctx *ctx, int tid) {
    void *out = NULL;
    int rc = lfq_try_dequeue_tid(ctx, tid, &out);
    if (rc > 0)
        return out;
    if (rc == 0)
        return NULL;
    return LFQ_ERROR;
}

void *lfq_dequeue(struct lfq_ctx *ctx) {
    void *out = NULL;
    int rc = lfq_try_dequeue(ctx, &out);
    if (rc > 0)
        return out;
    if (rc == 0)
        return NULL;
    return LFQ_ERROR;
}
