#define _GNU_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lfq.h"
#include "cross-platform.h"

#if !defined(_MSC_VER)
#include <pthread.h>
#endif

#ifndef MAX_PRODUCER
#define MAX_PRODUCER 100
#endif
#ifndef MAX_CONSUMER
#define MAX_CONSUMER 10
#endif
#ifndef ITEMS_PER_PRODUCER
#define ITEMS_PER_PRODUCER 100000
#endif

#define SOMEID 667814649u

static volatile uint64_t cn_added = 0;
static volatile uint64_t cn_deled = 0;
static volatile uint64_t sum_added = 0;
static volatile uint64_t sum_deled = 0;

static lfq_atomic_int_t producer_id_next = 0;
static lfq_atomic_int_t consumer_tid_next = 0;
static lfq_atomic_int_t producers_running = 0;
static lfq_atomic_int_t errors = 0;

struct user_data {
    uint32_t magic;
    uint32_t producer;
    uint32_t sequence;
};

THREAD_FN addq(void *data) {
    struct lfq_ctx *ctx = (struct lfq_ctx *)data;
    uint32_t producer = (uint32_t)(ATOMIC_ADD(&producer_id_next, 1) - 1);
    uint64_t local_added = 0;
    uint64_t local_sum = 0;

    for (uint32_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        struct user_data *p = (struct user_data *)malloc(sizeof(*p));
        if (!p) {
            ATOMIC_ADD(&errors, 1);
            break;
        }

        p->magic = SOMEID;
        p->producer = producer;
        p->sequence = i;

        int rc = lfq_enqueue(ctx, p);
        if (rc != 0) {
            fprintf(stderr, "lfq_enqueue failed: %d\n", rc);
            free(p);
            ATOMIC_ADD(&errors, 1);
            break;
        }

        ++local_added;
        local_sum += ((uint64_t)producer << 32) | i;
    }

    ATOMIC_ADD64(&cn_added, local_added);
    ATOMIC_ADD64(&sum_added, local_sum);
    ATOMIC_SUB(&producers_running, 1);
    return 0;
}

THREAD_FN delq(void *data) {
    struct lfq_ctx *ctx = (struct lfq_ctx *)data;
    int tid = ATOMIC_ADD(&consumer_tid_next, 1) - 1;
    uint64_t local_deleted = 0;
    uint64_t local_sum = 0;

    for (;;) {
        void *raw = NULL;
        int rc = lfq_try_dequeue_tid(ctx, tid, &raw);
        if (rc == 1) {
            struct user_data *p = (struct user_data *)raw;
            if (!p || p->magic != SOMEID) {
                fprintf(stderr, "data corruption detected\n");
                ATOMIC_ADD(&errors, 1);
                if (p)
                    free(p);
                break;
            }

            local_sum += ((uint64_t)p->producer << 32) | p->sequence;
            ++local_deleted;
            free(p);
            continue;
        }

        if (rc < 0) {
            fprintf(stderr, "lfq_try_dequeue_tid failed: %d\n", rc);
            ATOMIC_ADD(&errors, 1);
            break;
        }

        if (lfq_atomic_load_int(&producers_running) == 0)
            break;

        THREAD_YIELD();
    }

    ATOMIC_ADD64(&cn_deled, local_deleted);
    ATOMIC_ADD64(&sum_deled, local_sum);
    return 0;
}

int main(void) {
    struct lfq_ctx ctx;
    if (lfq_init(&ctx, MAX_CONSUMER) != 0) {
        fprintf(stderr, "lfq_init failed\n");
        return 1;
    }

    THREAD_TOKEN consumers[MAX_CONSUMER];
    THREAD_TOKEN producers[MAX_PRODUCER];
    int producer_created[MAX_PRODUCER] = {0};
    int consumer_created[MAX_CONSUMER] = {0};

    ATOMIC_ADD(&producers_running, MAX_PRODUCER);

    for (int i = 0; i < MAX_PRODUCER; ++i) {
#if defined(_MSC_VER)
        producers[i] = CreateThread(NULL, 0, addq, &ctx, 0, NULL);
        if (producers[i]) {
            producer_created[i] = 1;
        } else {
            ATOMIC_ADD(&errors, 1);
            ATOMIC_SUB(&producers_running, 1);
        }
#else
        if (pthread_create(&producers[i], NULL, addq, &ctx) == 0) {
            producer_created[i] = 1;
        } else {
            ATOMIC_ADD(&errors, 1);
            ATOMIC_SUB(&producers_running, 1);
        }
#endif
    }

    for (int i = 0; i < MAX_CONSUMER; ++i) {
#if defined(_MSC_VER)
        consumers[i] = CreateThread(NULL, 0, delq, &ctx, 0, NULL);
        if (consumers[i]) {
            consumer_created[i] = 1;
        } else {
            ATOMIC_ADD(&errors, 1);
        }
#else
        if (pthread_create(&consumers[i], NULL, delq, &ctx) == 0) {
            consumer_created[i] = 1;
        } else {
            ATOMIC_ADD(&errors, 1);
        }
#endif
    }

    for (int i = 0; i < MAX_PRODUCER; ++i) {
        if (producer_created[i])
            THREAD_WAIT(producers[i]);
    }

    for (int i = 0; i < MAX_CONSUMER; ++i) {
        if (consumer_created[i])
            THREAD_WAIT(consumers[i]);
    }

    /* Drain if thread creation failed; accounting still has to match. */
    for (;;) {
        void *raw = NULL;
        int rc = lfq_try_dequeue(&ctx, &raw);
        if (rc == 0)
            break;
        if (rc < 0) {
            ATOMIC_ADD(&errors, 1);
            break;
        }

        struct user_data *p = (struct user_data *)raw;
        if (!p || p->magic != SOMEID) {
            ATOMIC_ADD(&errors, 1);
            if (p)
                free(p);
            break;
        }

        ATOMIC_ADD64(&sum_deled, ((uint64_t)p->producer << 32) | p->sequence);
        ATOMIC_ADD64(&cn_deled, 1);
        free(p);
    }

    long retired = lfg_count_freelist(&ctx);
    int clean = lfq_clean(&ctx);
    int error_count = lfq_atomic_load_int(&errors);

    printf("push=%" PRIu64 " pop=%" PRIu64
           " sum_push=%" PRIu64 " sum_pop=%" PRIu64
           " retired=%ld clean=%d errors=%d\n",
           cn_added, cn_deled, sum_added, sum_deled, retired, clean, error_count);

    if (cn_added != cn_deled || sum_added != sum_deled || clean != 0 || error_count != 0) {
        fprintf(stderr, "multithread test FAILED\n");
        return 1;
    }

    printf("multithread test PASS\n");
    return 0;
}
