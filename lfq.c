#include "lfq.h"
#include <stdlib.h> 
#include <string.h>
#include <errno.h>

#include <stdio.h>


char dwcas( uint128_t * u, void * c1, void * c2, void * n1, void * n2 )
{
	// printf("%p %p %p %p %p %p\n", m1,m2,c1,c2,n1,n2);
	unsigned char ret;
	__asm__ __volatile__ (
		"lock cmpxchg16b %1\n\t"
		"setz %0\n"
		: "=q" ( ret )
		 ,"+m" ( *u )
		: "a" ( c1 ), "d" ( c2 )
		 ,"b" ( n1 ), "c" ( n2 )
		: "cc"
		);
	return ret;
}

int lfq_init(struct lfq_ctx *ctx) {
	struct lfq_node * firstnode = calloc(1,sizeof(struct lfq_node));
	if (!firstnode) 
		return -errno;
	
	memset(ctx,0,sizeof(struct lfq_ctx));
	ctx->head.c = ctx->tail = firstnode; 
	ctx->head.n = 0;
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
	
	__sync_add_and_fetch( &ctx->count, 1);
	
	return 0;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	void * ret=0;
	struct lfq_node * c, *n;
	
	do {
		c=ctx->head.c;
		n=ctx->head.n;
		if (!c)
			continue;
			
		if ( dwcas( &ctx->head.u, c, n, 0, n) ) {
			if (!n) {
				if (c->next) {
					n=c->next;
					ret=n->data;
					dwcas( &ctx->head.u, 0, 0, n, n->next);
				} else { // emp node
					dwcas( &ctx->head.u, 0, n, c, n);
					return 0;
				}
			} else {
				ret=n->data;
				dwcas( &ctx->head.u, 0, n, n, n->next);
			}
			break;
		}
		
	} while(1);
	
	__sync_sub_and_fetch( &ctx->count, 1);
free:
	free(c);
	return ret;
}

