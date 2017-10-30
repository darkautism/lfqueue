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

void enPool(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	struct lfq_node * p;
	do {
		p = ctx->fpt;
		if ( CAS(&ctx->fpt, p, lfn)) {
			p->free_next=lfn;
			break;	
		}
	} while(1);
}

void freePool(struct lfq_ctx *ctx, bool freeall ) {
	if (!CAS(&ctx->is_freeing, 0, 1))
		return; // this pool is not multithreading.
	struct lfq_node * prev, *p, * new_free_head;
	
	for ( int i = 0 ; i < MAXFREE ; i++ ) {
		p = ctx->fph;
		if ( (!p->can_free) || (!p->free_next) || inHP(ctx, p) )
			goto exit;
		ctx->fph = p->free_next;
		free(p);
	}
exit:
	ctx->is_freeing=false;
}

void safe_free(struct lfq_ctx *ctx, struct lfq_node * lfn) {
        if (!lfn->can_free)
                enPool(ctx, lfn);
        else if ( inHP(ctx, lfn) )
                enPool(ctx, lfn);
        else 
                free(lfn);

        freePool(ctx, false);
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
		struct lfq_node * walker = (struct lfq_node *)ctx->head, *tmp;
		while ( walker ) { // while still have node
			tmp = walker->next;
			safe_free(ctx, walker);
			walker = tmp;
		}
		ctx->tail = ctx->head = 0;
	}
	if ( ctx->fph && ctx->fpt ) {
		freePool(ctx, true);
		if ( ctx->fph != ctx->fpt ) {
			return -1;
		}
		free(ctx->fph); // free the empty node
		ctx->fph=ctx->fpt=0;
	}
	if ( !ctx->fph && !ctx->fpt && !ctx->tail && !ctx->head )
		memset(ctx,0,sizeof(struct lfq_ctx));
	else
		return -1;

	return 0;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * p;
	struct lfq_node * tmpnode = calloc(1,sizeof(struct lfq_node));
	if (!tmpnode)
		return -errno;

	tmpnode->data=data;
	do {
		p = (struct lfq_node *) ctx->tail;
		if ( CAS(&ctx->tail,p,tmpnode)) {
			p->next=tmpnode;
			break;	
		}
	} while(1);
	ATOMIC_ADD( &ctx->count, 1);
	return 0;
}

int alloc_tid(struct lfq_ctx *ctx) {
	for ( int i = 0 ; i < MAXHPSIZE ; i++ ) {
		if ( ctx->tid_map[i] == 0 ) {
			if ( CAS(&ctx->tid_map[i],0,1))
				return i;
		}
	}
	
	return -1;
}

void free_tid(struct lfq_ctx *ctx, int tid) {
	__sync_lock_release(&ctx->tid_map[tid]);
}

void * lfq_dequeue_tid(struct lfq_ctx *ctx, int tid ) {
	void * ret=0;
	struct lfq_node * p;
	struct lfq_node * a = (struct lfq_node *)ctx->head;	
	do {
		p = (struct lfq_node *) ctx->head;
		ctx->HP[tid] = p;
		if (p!= ctx->head)
			continue;
		if (p==0)
			return 0; // error case, this ctx is empty
		if (p->next==0){
			ctx->HP[tid] = 0;
			return 0;
		}
	} while(!CAS(&ctx->head, p, p->next));
	if ( ctx->head == 0 && p->next ) {// This is CAS's bug, why can be happened?
		printf("CAS bug?\n");
		ctx->head=p->next;
	}
	
	ctx->HP[tid] = 0;
	ret=p->next->data;
	p->next->can_free = true;
	ATOMIC_SUB( &ctx->count, 1);
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
