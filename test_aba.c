/*
 * Hazard-pointer / rapid-reuse stress test.
 *
 * This is intentionally not a formal linearizability proof. It maximizes
 * rapid queue-node retirement while user payloads are also allocated/freed.
 * Sanitizer CI is expected to catch stale accesses and data races.
 */
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

#ifndef ABA_THREADS
#define ABA_THREADS 16
#endif
#ifndef ABA_ITERATIONS
#define ABA_ITERATIONS 200000
#endif

#define LIVE_MAGIC 0xAB1AB1EFu
#define DEAD_MAGIC 0xDEADBEEFu

struct aba_item {
    uint32_t magic;
};

static volatile uint64_t total_enqueued = 0;
static volatile uint64_t total_dequeued = 0;
static lfq_atomic_int_t errors = 0;
static lfq_atomic_int_t cn_t = 0;
static struct lfq_ctx ctx;

THREAD_FN aba_thread(void *arg) {
    (void)arg;
    int tid = ATOMIC_ADD(&cn_t, 1) - 1;
    uint64_t local_enq = 0;
    uint64_t local_deq = 0;

    for (int i = 0; i < ABA_ITERATIONS && lfq_atomic_load_int(&errors) == 0; ++i) {
        struct aba_item *item = (struct aba_item *)malloc(sizeof(*item));
        if (!item) {
            ATOMIC_ADD(&errors, 1);
            break;
        }
        item->magic = LIVE_MAGIC;

        if (lfq_enqueue(&ctx, item) != 0) {
            free(item);
            ATOMIC_ADD(&errors, 1);
            break;
        }
        ++local_enq;

        for (;;) {
            void *raw = NULL;
            int rc = lfq_try_dequeue_tid(&ctx, tid, &raw);
            if (rc == 0)
                continue;
            if (rc < 0) {
                fprintf(stderr, "dequeue error %d tid=%d\n", rc, tid);
                ATOMIC_ADD(&errors, 1);
                break;
            }

            struct aba_item *got = (struct aba_item *)raw;
            if (!got || got->magic != LIVE_MAGIC) {
                fprintf(stderr, "payload corruption tid=%d iter=%d\n", tid, i);
                ATOMIC_ADD(&errors, 1);
                if (got)
                    free(got);
                break;
            }

            got->magic = DEAD_MAGIC;
            free(got);
            ++local_deq;
            break;
        }
    }

    ATOMIC_ADD64(&total_enqueued, local_enq);
    ATOMIC_ADD64(&total_dequeued, local_deq);
    return 0;
}

int main(void) {
    if (lfq_init(&ctx, ABA_THREADS) != 0) {
        fprintf(stderr, "lfq_init failed\n");
        return 1;
    }

    THREAD_TOKEN threads[ABA_THREADS];
    int created[ABA_THREADS] = {0};

    for (int i = 0; i < ABA_THREADS; ++i) {
#if defined(_MSC_VER)
        threads[i] = CreateThread(NULL, 0, aba_thread, NULL, 0, NULL);
        if (threads[i]) {
            created[i] = 1;
        } else {
            ATOMIC_ADD(&errors, 1);
        }
#else
        if (pthread_create(&threads[i], NULL, aba_thread, NULL) == 0) {
            created[i] = 1;
        } else {
            ATOMIC_ADD(&errors, 1);
        }
#endif
    }

    for (int i = 0; i < ABA_THREADS; ++i) {
        if (created[i])
            THREAD_WAIT(threads[i]);
    }

    for (;;) {
        void *raw = NULL;
        int rc = lfq_try_dequeue(&ctx, &raw);
        if (rc == 0)
            break;
        if (rc < 0) {
            ATOMIC_ADD(&errors, 1);
            break;
        }

        struct aba_item *item = (struct aba_item *)raw;
        if (!item || item->magic != LIVE_MAGIC) {
            ATOMIC_ADD(&errors, 1);
            if (item)
                free(item);
            break;
        }

        item->magic = DEAD_MAGIC;
        free(item);
        ++total_dequeued;
    }

    long retired = lfg_count_freelist(&ctx);
    int clean = lfq_clean(&ctx);
    int error_count = lfq_atomic_load_int(&errors);

    printf("ABA stress: enqueued=%" PRIu64 " dequeued=%" PRIu64
           " retired=%ld clean=%d errors=%d\n",
           total_enqueued, total_dequeued, retired, clean, error_count);

    if (error_count != 0 || total_enqueued != total_dequeued || clean != 0) {
        fprintf(stderr, "ABA stress FAILED\n");
        return 1;
    }

    printf("ABA stress PASS\n");
    return 0;
}
