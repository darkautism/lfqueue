#include "lfq.h"
#include <stdlib.h> 
#include <string.h>
#include <errno.h>

#include <stdio.h>
#include <unistd.h>



int inHP(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	for ( int i = 0 ; i < MAXHPSIZE ; i++ )
		if (ctx->HP[i] == lfn)
			return 1;
	
	return 0;
}

void enPool(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	struct lfq_node * p;
	do {
		p = ctx->fpt;
		if ( __sync_bool_compare_and_swap(&ctx->fpt, p, lfn)) {
			p->next=lfn;
			break;	
		}
	} while(1);
}

void freePool(struct lfq_ctx *ctx) {
	if (!__sync_bool_compare_and_swap(&ctx->is_freeing, 0, 1))
		return; // this pool is not multithreading.
	struct lfq_node * prev, *p, * new_free_head;
	
	for ( int i = 0 ; i < MAXFREE ; i++ ) {
		p = ctx->fph;
		if ( (!p->can_free) || (!p->next) || inHP(ctx, p) )
			return;
		ctx->fph = p->next;
		free(p);
	}
}


int lfq_init(struct lfq_ctx *ctx) {
	struct lfq_node * tmpnode = calloc(1,sizeof(struct lfq_node));
	if (!tmpnode) 
		return -errno;
		
	struct lfq_node * free_pool_node = calloc(1,sizeof(struct lfq_node));
	if (!free_pool_node) 
		return -errno;
	memset(ctx,0,sizeof(struct lfq_ctx));
	ctx->head=ctx->tail=tmpnode;
	ctx->fph=ctx->fpt=free_pool_node;
	return 0;
}

int lfq_clean(struct lfq_ctx *ctx){
	if ( ctx->tail && ctx->head ) { // if have data in queue
		struct lfq_node * walker = ctx->head, *tmp;
		while ( walker != ctx->tail ) { // while still have node
			tmp = walker->next;
			free(walker);
			walker=tmp;
		}
		free(ctx->head); // free the empty node
		freePool(ctx);
		free(ctx->fph); // free the empty node
		memset(ctx,0,sizeof(struct lfq_ctx));
	}
	
	return 0;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * p;
	struct lfq_node * tmpnode = calloc(1,sizeof(struct lfq_node));
	if (!tmpnode)
		return -errno;
	
	tmpnode->data=data;
	do {
		p = ctx->tail;
		if ( __sync_bool_compare_and_swap(&ctx->tail,p,tmpnode)) {
			p->next=tmpnode;
			break;	
		}
	} while(1);
	__sync_add_and_fetch( &ctx->count, 1);
	return 0;
}


void safe_free(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	if (!lfn->can_free)
		enPool(ctx, lfn);
	else if ( inHP(ctx, lfn) )
		enPool(ctx, lfn);
	else
		free(lfn);
	
	freePool(ctx);
}

int alloc_tid(struct lfq_ctx *ctx) {
	for ( int i = 0 ; i < MAXHPSIZE ; i++ ) {
		if ( ctx->tid_map[i] == 0 ) {
			if ( __sync_bool_compare_and_swap(&ctx->tid_map[i],0,1))
				return i;
		}
	}
	
	return -1;
}

void free_tid(struct lfq_ctx *ctx, int tid) {
	ctx->tid_map[tid] = 0;
}

void * lfq_dequeue_tid(struct lfq_ctx *ctx, int tid ) {
	void * ret=0;
	struct lfq_node * p;
	
	do {
		p = ctx->head;
		ctx->HP[tid] = p;
		if (p!= ctx->head)
			continue;
		if (p->next == 0){
			ctx->HP[tid] = 0;
			return 0;
		}
		
	} while(!__sync_bool_compare_and_swap(&ctx->head, p, p->next));
	
	ctx->HP[tid] = 0;
	ret=p->next->data;
	p->next->can_free = 1;
	__sync_sub_and_fetch( &ctx->count, 1);
	safe_free(ctx, p);
	return ret;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	int tid = alloc_tid(ctx);
	if (tid==-1)
		return (void *)-1; // To many thread race
		
	void * ret = lfq_dequeue_tid(ctx, tid);
	free_tid(ctx, tid);
	return ret;
}