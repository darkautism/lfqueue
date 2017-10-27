#ifndef __LFQ_H__
#define __LFQ_H__
#include <stddef.h>

struct lfq_node{
	void * data;
	struct lfq_node * next;
};

#ifndef uint128_t
#define uint128_t unsigned __int128
#endif

struct lfq_ctx{
	union {
		uint128_t u;
	    struct {
			struct lfq_node* c;
			struct lfq_node* n;
		};
	} __attribute__ (( __aligned__( 16 ) )) head;

	struct lfq_node * tail;
	
	int count;
};


int lfq_init(struct lfq_ctx *ctx);
int lfq_clean(struct lfq_ctx *ctx);
int lfq_enqueue(struct lfq_ctx *ctx, void * data);
void * lfq_dequeue(struct lfq_ctx *ctx );
#endif