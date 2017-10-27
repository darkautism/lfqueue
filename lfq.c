#include "lfq.h"
#include <stdlib.h> 
#include <string.h>
#include <errno.h>

#include <stdio.h>

char dwcas(size_t data[], struct lfq_node * current_head, struct lfq_node * next, struct lfq_node * new_head, struct lfq_node * new_next )
{
	unsigned char ret;
	__asm__ __volatile__ (
		"lock cmpxchg16b %1\n\t"
		"setz %0\n"
		: "=q" ( ret )
		 ,"+m" ( data[0] ),"+m" ( data[1] )
		: "a" ( current_head ), "d" ( next )
		 ,"b" ( new_head ), "c" ( new_next )
		: "cc"
		);
	return ret;
}

int lfq_init(struct lfq_ctx *ctx) {
	struct lfq_node * tmpnode = calloc(1,sizeof(struct lfq_node));
	if (!tmpnode) 
		return -errno;
	
	memset(ctx,0,sizeof(struct lfq_ctx));
	ctx->tail = ctx->head.c = tmpnode;
	return 0;
}

int lfq_clean(struct lfq_ctx *ctx){
	if ( ctx->tail && ctx->head.c ) { // if have data in queue
		struct lfq_node * walker = ctx->head.c, *tmp;
		while ( walker != ctx->tail ) { // while still have node
			tmp = walker->next;
			free(walker);
			walker=tmp;
		}
		free(ctx->head.c); // free the empty node
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
	//printf("z\n");
	if (p == ctx->head.c) {
		if (ctx->head.n != 0)
			printf("BUG!\n");
		
		if (!dwcas( ctx->head.b16, p, ctx->head.n, p, tmpnode))
			printf("BUG!\n");
		printf("EMP NODE!\n");
	}
	
	__sync_add_and_fetch( &ctx->count, 1);
	return 0;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	void * ret=0;
	struct lfq_node * c, *n;
	do {
		c=ctx->head.c;
		n=c->next;
		if (!n)
			return 0; // there has no data
	} while ( ! dwcas( ctx->head.b16, c, n, n, n->next) );
	
	// printf("%d!\n", ctx->count);
	ret=n->data;
	__sync_sub_and_fetch( &ctx->count, 1);
	free(c);
	/*
	struct lfq_node * p;
	do {
		p = ctx->head;
	} while(p==0 || !__sync_bool_compare_and_swap(&ctx->head,p,0));
	
	if( p->next==0)	{
		ctx->head=p;
		return 0;
	}
	ret=p->next->data;
	ctx->head=p->next;
	__sync_sub_and_fetch( &ctx->count, 1);

	free(p);
	*/
	return ret;
}

