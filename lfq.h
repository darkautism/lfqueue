#ifndef __LFQ_H__
#define __LFQ_H__

#define __IFQ_NODE_STATE_PREADD 0
#define __IFQ_NODE_STATE_INQUEUE 1
#define __IFQ_NODE_STATE_SWAP 2
#define __IFQ_NODE_STATE_DELETED 3

struct lfq_node{
	void * data;
	volatile int cn_donotfree;
	struct lfq_node * next;
};

struct lfq_ctx{
	struct lfq_node * head;
	struct lfq_node * __free_head;
	struct lfq_node * tail;
	int count;
	int cn_nonfree;
};
int lfq_init(struct lfq_ctx *ctx);
int lfq_clean(struct lfq_ctx *ctx);
int lfq_enqueue(struct lfq_ctx *ctx, void * data);
void * lfq_dequeue(struct lfq_ctx *ctx );
#endif