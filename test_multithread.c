#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include "lfq.h"
#include "cross-platform.h"

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__
#include <unistd.h>
#include <pthread.h>
#else
#include <windows.h>
#endif


#ifndef MAX_PRODUCER
#define MAX_PRODUCER 100
#endif
#ifndef MAX_CONSUMER
#define MAX_CONSUMER 10
#endif


#define SOMEID 667814649

volatile uint64_t cn_added = 0;
volatile uint64_t cn_deled = 0;

volatile int cn_t = 0;
volatile int cn_producer = 0;

struct user_data{
	long data;
};

THREAD_FN addq( void * data ) {
	struct lfq_ctx * ctx = data;
	int ret = 0;
	long added;
	for (added = 0 ; added < 500000 ; added++) {
		struct user_data * p = malloc(sizeof(struct user_data));
		p->data=SOMEID;
		if ( ( ret = lfq_enqueue(ctx,p) ) != 0 ) {
			printf("lfq_enqueue failed, reason:%s\n", strerror(-ret));
			ATOMIC_ADD64(&cn_added, added);
			ATOMIC_SUB(&cn_producer, 1);
			return 0;
		}
	}
	ATOMIC_ADD64(&cn_added, added);
	ATOMIC_SUB(&cn_producer, 1);
	printf("Producer thread [%lu] exited! Still %d running...\n",THREAD_ID(), cn_producer);
	return 0;
}

THREAD_FN delq(void * data) {
	struct lfq_ctx * ctx = data;
	struct user_data * p;
	int tid = ATOMIC_ADD(&cn_t, 1);

	long deleted = 0;
	while(1) {
		p = lfq_dequeue_tid(ctx, tid);
		if (p) {
			if (p->data!=SOMEID){
				printf("data wrong!!\n");
				exit(1);
			}

			free(p);
			deleted++;
		} else {
			if (ctx->count || cn_producer)
				THREAD_YIELD(); // queue is empty, release CPU slice
			else
				break; // queue is empty and no more producers
		}
	}

	ATOMIC_ADD64(&cn_deled, deleted);

	// p = lfq_dequeue_tid(ctx, tid);
	printf("Consumer thread [%lu] exited %d\n",THREAD_ID(),cn_producer);
	return 0;
}

int main() {
	struct lfq_ctx ctx;
	int i = 0;
	lfq_init(&ctx, MAX_CONSUMER);
	THREAD_TOKEN thread_d[MAX_CONSUMER];
	THREAD_TOKEN thread_a[MAX_PRODUCER];
	
	ATOMIC_ADD(&cn_producer, 1);
	for ( i = 0 ; i < MAX_CONSUMER ; i++ ) {
#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__
		pthread_create(&thread_d[i], NULL, delq, (void*)&ctx);
#else
#pragma warning(disable:4133)
		thread_d[i] = CreateThread(NULL, 0, delq, &ctx, 0, 0);
#endif
	}
	
	for ( i = 0 ; i < MAX_PRODUCER ; i++ ){
		ATOMIC_ADD(&cn_producer, 1);
#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__
		pthread_create(&thread_a[i], NULL, addq, (void*)&ctx);
#else
#pragma warning(disable:4133)
		thread_a[i] = CreateThread(NULL, 0, addq, &ctx, 0, 0);
#endif
		
	}
	
	ATOMIC_SUB(&cn_producer, 1);
	
	for (i = 0; i < MAX_PRODUCER; i++)
		THREAD_WAIT(thread_a[i]);
	
	for ( i = 0 ; i < MAX_CONSUMER ; i++ )
		THREAD_WAIT(thread_d[i]);

	long freecount = lfg_count_freelist(&ctx);
	int clean = lfq_clean(&ctx);

	printf("Total push %"PRId64" elements, pop %"PRId64" elements. freelist=%ld, clean = %d\n", cn_added, cn_deled, freecount, clean);
	if ( cn_added == cn_deled )
		printf("Test PASS!!\n");
	else
		printf("Test Failed!!\n");
	lfq_clean(&ctx);
	return (cn_added != cn_deled);
}
