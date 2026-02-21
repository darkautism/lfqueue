/*
 * ABA stress test for lfqueue
 *
 * The ABA problem: a CAS on a pointer can succeed spuriously when:
 *   1. Thread 1 reads head = node_A
 *   2. Thread 2 dequeues A, frees it; malloc() returns the same address for
 *      a new node B; B is enqueued, making head cycle back to address A
 *   3. Thread 1's CAS(&head, A, A->next) succeeds even though the node at
 *      address A is now logically B — A->next is stale/garbage.
 *
 * To maximize ABA probability this test uses:
 *   - All threads as both producers AND consumers (tight enq+deq loops)
 *   - No thread_yield between enqueue and dequeue (maximises racing)
 *   - Very small queue depth (each thread keeps at most 1 item in-flight)
 *   - Many threads competing on the same head pointer
 *   - Magic values in each node to detect data corruption caused by ABA
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "lfq.h"
#include "cross-platform.h"

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__
#include <unistd.h>
#include <pthread.h>
#else
#include <windows.h>
#endif

#ifndef ABA_THREADS
#define ABA_THREADS 16
#endif

#ifndef ABA_ITERATIONS
#define ABA_ITERATIONS 500000
#endif

/* Two distinct magic values — LIVE (in queue) and DEAD (already freed).
 * A read of DEAD_MAGIC on a dequeued item means use-after-free / ABA
 * corruption. */
#define LIVE_MAGIC 0xAB1AB1EFUL
#define DEAD_MAGIC 0xDEADBEEFUL

struct aba_item {
	volatile uint32_t magic;
};

static volatile uint64_t total_enqueued = 0;
static volatile uint64_t total_dequeued = 0;
static volatile int      errors = 0;
static volatile int      cn_t   = 0;   /* tid allocator */

static struct lfq_ctx ctx;

/*
 * Each thread alternates between enqueue and spin-dequeue in a tight loop.
 * This keeps the queue depth near zero, so the same pointer addresses cycle
 * through head rapidly — exactly the condition that triggers ABA.
 */
THREAD_FN aba_thread(void *arg) {
	(void)arg;
	/* Allocate a dedicated hazard-pointer slot for this thread */
	int tid = ATOMIC_ADD(&cn_t, 1) - 1;

	uint64_t local_enq = 0, local_deq = 0;

	for (int i = 0; i < ABA_ITERATIONS && !errors; i++) {
		/* --- Enqueue a fresh item --- */
		struct aba_item *item = malloc(sizeof(struct aba_item));
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
		local_enq++;

		/*
		 * --- Spin-dequeue (no yield) ---
		 * By immediately spinning for a result with no sleep we keep
		 * maximum pressure on the head CAS, creating the window for ABA:
		 *   our enqueued node may be stolen and its memory recycled before
		 *   we manage to dequeue it ourselves.
		 */
		struct aba_item *got;
		do {
			got = lfq_dequeue_tid(&ctx, tid);
		} while (got == NULL);

		/* Validate: corruption means ABA or use-after-free occurred */
		if (got->magic != LIVE_MAGIC) {
			printf("ABA corruption detected! "
			       "magic=0x%08X (expected 0x%08X) iter=%d tid=%d\n",
			       got->magic, (uint32_t)LIVE_MAGIC, i, tid);
			ATOMIC_ADD(&errors, 1);
		}
		/* Poison before free to detect future use-after-free reads */
		got->magic = DEAD_MAGIC;
		free(got);
		local_deq++;
	}

	ATOMIC_ADD64(&total_enqueued, local_enq);
	ATOMIC_ADD64(&total_dequeued, local_deq);
	return 0;
}

int main(void) {
	printf("ABA stress test: %d threads x %d iterations each\n",
	       ABA_THREADS, ABA_ITERATIONS);
	printf("(tight enq+deq loops, no yield — maximises head-pointer reuse)\n");

	if (lfq_init(&ctx, ABA_THREADS) != 0) {
		fprintf(stderr, "lfq_init failed\n");
		return 1;
	}

	THREAD_TOKEN threads[ABA_THREADS];
	for (int i = 0; i < ABA_THREADS; i++) {
#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__
		pthread_create(&threads[i], NULL, aba_thread, NULL);
#else
#pragma warning(disable:4133)
		threads[i] = CreateThread(NULL, 0, aba_thread, NULL, 0, 0);
#endif
	}

	for (int i = 0; i < ABA_THREADS; i++)
		THREAD_WAIT(threads[i]);

	/* Drain any items left in the queue after all threads exit */
	struct aba_item *item;
	int drain_tid = 0; /* safe: all thread tids have been released */
	while ((item = lfq_dequeue_tid(&ctx, drain_tid)) != NULL) {
		if (item->magic != LIVE_MAGIC) {
			printf("ABA corruption in drain! magic=0x%08X\n", item->magic);
			errors++;
		}
		item->magic = DEAD_MAGIC;
		free(item);
		total_dequeued++;
	}

	long freecount = lfg_count_freelist(&ctx);
	int  clean     = lfq_clean(&ctx);

	printf("Enqueued=%" PRId64 " Dequeued=%" PRId64
	       " freelist=%ld clean=%d\n",
	       total_enqueued, total_dequeued, freecount, clean);

	if (errors)
		printf("ABA Test FAILED!! (%d corruption(s) detected)\n", errors);
	else
		printf("ABA Test PASS!!\n");

	return errors != 0;
}
