#include "lfq.h"
#include <stdlib.h> 
#include <string.h>

int lfq_init(struct lfq_ctx *ctx) {
	struct lfq_node * tmpnode = malloc(sizeof(struct lfq_node)); // Fix it
	memset(ctx,0,sizeof(struct lfq_ctx));
	ctx->head=ctx->tail=tmpnode;
	return 0;
}

int lfq_clean(struct lfq_ctx *ctx){
	if ( ctx->count == 0 )
		free(ctx->head);
	return ctx->count;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * tmpnode = malloc(sizeof(struct lfq_node)); // Fix it
	struct lfq_node * p;
	int success = 0;
	tmpnode->next=0;
	tmpnode->data=data;
	do {
		p = ctx->tail;
		__sync_synchronize();
		success = __sync_bool_compare_and_swap(&p->next,0,tmpnode);
		if (success)
			__sync_bool_compare_and_swap(&ctx->tail,p,tmpnode);
	} while(!success);
	__sync_add_and_fetch( &ctx->count, 1);
	return 0;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	void * ret=0;
	struct lfq_node * p;
	int success = 0;
	do {
		p = ctx->head;
		if (p->next)
			success = __sync_bool_compare_and_swap(&ctx->head,p,p->next);
		else
			return 0;
	} while(!success);
	if (p){
		ret=p->next->data;
		free(p);
		__sync_sub_and_fetch( &ctx->count, 1);
	}
	return ret;
}
