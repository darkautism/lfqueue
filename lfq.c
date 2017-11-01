#include "cross-platform.h"
#include "lfq.h"
#include <errno.h>

#include <stdio.h>
int inHP(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	for ( int i = 0 ; i < MAXHPSIZE ; i++ )
		if (ctx->HP[i] == lfn)
			return 1;
	
	return 0;
}

void enpool(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	struct lfq_node * p;
	do {
		p = ctx->fpt;
	} while(!CAS(&ctx->fpt, p, lfn));
	ATOMIC_SET(&p->free_next, lfn);
}

void free_pool(struct lfq_ctx *ctx, bool freeall ) {
	if (!CAS(&ctx->is_freeing, 0, 1))
		return; // this pool free is not support multithreading.
	struct lfq_node * p;
	
	for ( int i = 0 ; i < MAXFREE || freeall ; i++ ) {
		p = ctx->fph;
		if ( (!p->can_free) || (!p->free_next) || inHP(ctx, p) )
			goto exit;
		ctx->fph = p->free_next;
		mb(); 
		free(p);
	}
exit:
	ATOMIC_SET(&ctx->is_freeing,false);
}

void safe_free(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	// if (!lfn->can_free)
		enpool(ctx, lfn);
	// else if ( inHP(ctx, lfn) )
	// 	enpool(ctx, lfn);
	// else 
	// 	free(lfn);
		
	free_pool(ctx, false);
}


int lfq_init(struct lfq_ctx *ctx) {
	struct lfq_node * tmpnode = calloc(1,sizeof(struct lfq_node));
	if (!tmpnode) 
		return -errno;
		
	struct lfq_node * free_pool_node = calloc(1,sizeof(struct lfq_node));
	if (!free_pool_node) 
		return -errno;

	tmpnode->can_free = free_pool_node->can_free = true;
	memset(ctx,0,sizeof(struct lfq_ctx));
	ctx->head=ctx->tail=tmpnode;
	ctx->fph=ctx->fpt=free_pool_node;
	return 0;
}
int lfq_clean(struct lfq_ctx *ctx){
	if ( ctx->tail && ctx->head ) { // if have data in queue
		struct lfq_node *tmp;
		while ( (struct lfq_node *) ctx->head ) { // while still have node
			tmp = (struct lfq_node *) ctx->head->next;
			safe_free(ctx, (struct lfq_node *)ctx->head);
			ctx->head = tmp;
		}
		ctx->tail = 0;
	}
	if ( ctx->fph && ctx->fpt ) {
		free_pool(ctx, true);
		if ( ctx->fph != ctx->fpt )
			return -1;
		free(ctx->fpt); // free the empty node
		ctx->fph=ctx->fpt=0;
	}
	if ( !ctx->fph && !ctx->fpt )
		memset(ctx,0,sizeof(struct lfq_ctx));
	else
		return -1;
		
	return 0;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * p;
	struct lfq_node * insert_node = calloc(1,sizeof(struct lfq_node));
	if (!insert_node)
		return -errno;
	insert_node->data=data;
	do {
		p = (struct lfq_node *) ctx->tail;
	} while(!CAS(&ctx->tail,p,insert_node));
	p->next = insert_node;
	mb(); // memory barrier
	ATOMIC_ADD( &ctx->count, 1);
	return 0;
}

int alloc_tid(struct lfq_ctx *ctx) {
	for ( int i = 0 ; i < MAXHPSIZE ; i++ )
		if ( ctx->tid_map[i] == 0 )
			if ( CAS(&ctx->tid_map[i],0,1))
				return i;
	return -1;
}

void free_tid(struct lfq_ctx *ctx, int tid) {
	ATOMIC_RELEASE(&ctx->tid_map[tid]);
}

void * lfq_dequeue_tid(struct lfq_ctx *ctx, int tid ) {
	void * ret=0;
	struct lfq_node * p, * pn;
	do {
		p = (struct lfq_node *) ctx->head;
		ctx->HP[tid] = p;
		if (p != ctx->head)
			continue;
		pn = (struct lfq_node *) p->next;
		if (pn==0 || pn != p->next){
			ctx->HP[tid] = 0;
			return 0;
		}
	} while( ! CAS(&ctx->head, p, pn) );
	
	ctx->HP[tid] = 0;
	ret=(void *)pn->data;
	ATOMIC_SET(&pn->can_free, true);
	ATOMIC_SUB( &ctx->count, 1 );
	mb();
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
