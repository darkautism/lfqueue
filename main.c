#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include "lfq.h"

volatile uint64_t cn_added = 0;
volatile uint64_t cn_deled = 0;
volatile int cn_producer = 0;

#define TESTROUND 4
struct user_data{
	long data;
};
void * addq( void * data ) {
	struct lfq_ctx * ctx = data;
	struct user_data * p;
	long i;
	for ( i = 0 ; i < 5000 ; i++) {
		p = malloc(sizeof(struct user_data));
		p->data=i;
		lfq_enqueue(ctx,p);
		__sync_add_and_fetch(&cn_added, 1);
		// printf("push : %ld\n", p->data);
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
			// printf("pop : %ld\n", p->data);
			free(p);
			__sync_add_and_fetch(&cn_deled, 1);			
		}
		// printf("still %d, %p\n",ctx->count, p );
		sleep(0);
	}
	printf("Consumer thread [%lu] exited\n",pthread_self());
}

int main() {
	struct lfq_ctx ctx;
	lfq_init(&ctx);
	pthread_t thread_d[TESTROUND];
	pthread_t thread_a[TESTROUND];
	
	for ( int i = 0 ; i < TESTROUND ; i++ ){
		__sync_add_and_fetch(&cn_producer, 1);
		pthread_create(&thread_a[i], NULL , addq , (void*) &ctx);
		pthread_create(&thread_d[i], NULL , delq , (void*) &ctx);
	}
	
	for ( int i = 0 ; i < TESTROUND ; i++ )
		pthread_join(thread_a[i], NULL);
	
	for ( int i = 0 ; i < TESTROUND ; i++ )
		pthread_join(thread_d[i], NULL);
	
	printf("Total push %"PRId64" elements, pop %"PRId64" elements.\n", cn_added, cn_deled);
	if ( cn_added == cn_deled )
		printf("Test PASS!!\n");
	else
		printf("Test Failed!!\n");
	lfq_clean(&ctx);
	return (cn_added == cn_deled);
}
