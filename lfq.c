#include "lfq.h"
#include <stdlib.h> 
#include <string.h>
#include <errno.h>
#include <stdio.h>


void free_some(struct lfq_ctx *ctx, int min_limit) {
	struct lfq_node * p;
	while ( ctx->cn_nonfree > min_limit ) {
		do {
			p = ctx->__free_head;
			if (!p->next)
				return;
		} while( ! __sync_bool_compare_and_swap(&ctx->__free_head,p,p->next) );
		// while (p->cn_donotfree) sleep(0); // Fix it: This value can been negative. How does it happened?
		free(p);
		__sync_sub_and_fetch( &ctx->cn_nonfree, 1);
	}
	
}

int lfq_init(struct lfq_ctx *ctx) {
	struct lfq_node * tmpnode = malloc(sizeof(struct lfq_node));
	if (!tmpnode) 
		return -errno;
	
	memset(ctx,0,sizeof(struct lfq_ctx));
	ctx->__free_head=ctx->head=ctx->tail=tmpnode;
	return 0;
}

int lfq_clean(struct lfq_ctx *ctx){
	free_some(ctx, 0);
	if ( !ctx->count )
		free(ctx->head);
	return ctx->count;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * p;
	struct lfq_node * tmpnode = malloc(sizeof(struct lfq_node));
	if (!tmpnode) 
		return -errno;
	
	// if (ctx->count > 16384) free_some( ctx, 0);
	
	memset(tmpnode,0,sizeof(struct lfq_node));
	tmpnode->data=data;
	do {
		p = ctx->tail;
		if (__sync_bool_compare_and_swap(&p->next,0,tmpnode)) 
			if ( __sync_bool_compare_and_swap(&ctx->tail,p,tmpnode) ) 
				break;
			else {
				p->next=0; // Fix it: This should not happened, but it just happened!
				printf("swap failed!\n");
			}
	} while(1);
	__sync_add_and_fetch( &ctx->count, 1);
	return 0;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	void * ret=0;
	struct lfq_node * p;
	do {
		p = ctx->head;
		if (!p->next)
			return 0;
	} while(!__sync_bool_compare_and_swap(&ctx->head,p,p->next));
	ret=p->next->data;
	__sync_add_and_fetch( &ctx->cn_nonfree, 1);
	__sync_sub_and_fetch( &ctx->count, 1);
	// free(p); // Fix it: Memory corruption or memory leak.
	return ret;
}
