#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include "lfq.h"

#ifndef MAX_PRODUCER
#define MAX_PRODUCER 100
#endif
#ifndef MAX_CONSUMER
#define MAX_CONSUMER 10
#endif

volatile uint64_t cn_added = 0;
volatile uint64_t cn_deled = 0;
volatile int cn_producer = 0;

struct user_data{
	long data;
};

void * addq( void * data ) {
	struct lfq_ctx * ctx = data;
	struct user_data * p=(struct user_data *)0xff;
	long i;
	int ret = 0;
	for ( i = 0 ; i < 100000 ; i++) {
		p = malloc(sizeof(struct user_data));
		p->data=i;
		if ( ( ret = lfq_enqueue(ctx,p) ) != 0 ) {
			printf("lfq_enqueue failed, reason:%s", strerror(-ret));
			__sync_sub_and_fetch(&cn_producer, 1);
			return 0;
		}
		__sync_add_and_fetch(&cn_added, 1);
	}
	__sync_sub_and_fetch(&cn_producer, 1);
	printf("Producer thread [%lu] exited! Still %d running...\n",pthread_self(), cn_producer);
}

void * delq( void * data ) {
	struct lfq_ctx * ctx = data;
	struct user_data * p;
	while(ctx->count || cn_producer) {
		p = lfq_dequeue(ctx);
		if (p) {
			free(p);
			__sync_add_and_fetch(&cn_deled, 1);			
		}
		sleep(0);
	}

	//p = lfq_dequeue(ctx);
	printf("Consumer thread [%lu] exited %d %d\n",pthread_self(),cn_producer,ctx->count);
}

int main() {
	struct lfq_ctx ctx;
	int i=0;
	lfq_init(&ctx);
	pthread_t thread_d[MAX_CONSUMER];
	pthread_t thread_a[MAX_PRODUCER];
	
	__sync_add_and_fetch(&cn_producer, 1);
	for ( i = 0 ; i < MAX_CONSUMER ; i++ )
		pthread_create(&thread_d[i], NULL , delq , (void*) &ctx);
	
	for ( i = 0 ; i < MAX_PRODUCER ; i++ ){
		__sync_add_and_fetch(&cn_producer, 1);
		pthread_create(&thread_a[i], NULL , addq , (void*) &ctx);
	}
	
	__sync_sub_and_fetch(&cn_producer, 1);
	
	for ( i = 0 ; i < MAX_PRODUCER ; i++ )
		pthread_join(thread_a[i], NULL);
	
	for ( i = 0 ; i < MAX_CONSUMER ; i++ )
		pthread_join(thread_d[i], NULL);
	
	printf("Total push %"PRId64" elements, pop %"PRId64" elements.\n", cn_added, cn_deled);
	if ( cn_added == cn_deled )
		printf("Test PASS!!\n");
	else
		printf("Test Failed!!\n");
	lfq_clean(&ctx);
	return (cn_added != cn_deled);
}
