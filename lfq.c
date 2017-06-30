#include "lfq.h"
#include <stdlib.h> 
#include <string.h>
#include <errno.h>
#include <stdio.h>

int lfq_init(struct lfq_ctx *ctx) {
	int i;

	struct lfq_node * tmp_node=NULL;
	struct lfq_node * prev_node=NULL;

	memset(ctx,0,sizeof(struct lfq_ctx));
	
	for(i=0;i< RING_BUF_SIZE;i++)
	{
		tmp_node = malloc(sizeof(struct lfq_node));
		
		if (!tmp_node) 
			return -errno;

		memset(tmp_node,0,sizeof(struct lfq_node));

		if(i==0)
			ctx->head=tmp_node;
		
		if(prev_node)
			prev_node->next=tmp_node;

		prev_node=tmp_node;	
	}

	prev_node->next=ctx->tail=ctx->head;

	return 0;
}

int lfq_clean(struct lfq_ctx *ctx){
	if ( !ctx->count )
		free(ctx->head);
	return ctx->count;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * p;
	do {

		do{
			p = ctx->tail;
			//p->data=NULL;
		}while (p->next==ctx->head || p->data!=NULL);

		if ( __sync_bool_compare_and_swap(&ctx->tail,p,p->next)) 
		{
			p->data=data;
			break;	
		}
	} while(1);
	//printf("e\n");
	__sync_add_and_fetch( &ctx->count, 1);
	return 0;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	void * ret=0;
	void * data;
	struct lfq_node * p;
	do {
		p = ctx->head;
		ret=p->data;
		if( p==ctx->tail || ret ==NULL)
			return NULL;

		if (__sync_bool_compare_and_swap(&ctx->head,p,p->next))
		{
			p->data=NULL;
			break;
		}
	}while(1);
	//printf("d\n");
	__sync_sub_and_fetch( &ctx->count, 1);

	return ret;
}
