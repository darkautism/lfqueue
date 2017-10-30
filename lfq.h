#ifndef __LFQ_H__
#define __LFQ_H__

struct lfq_node{
	void * data;
	struct lfq_node * next;
	volatile int can_free;
	struct lfq_node * free_next;
};

#define MAXHPSIZE 256
#define MAXFREE 10

struct lfq_ctx{
	volatile struct lfq_node * head;
	volatile struct lfq_node * tail;
	int count;
	struct lfq_node * HP[MAXHPSIZE];
	int tid_map[MAXHPSIZE];
	int is_freeing;
	struct lfq_node * fph; // free pool head
	struct lfq_node * fpt; // free pool tail
};

int lfq_init(struct lfq_ctx *ctx);
int lfq_clean(struct lfq_ctx *ctx);
int lfq_enqueue(struct lfq_ctx *ctx, void * data);
void * lfq_dequeue(struct lfq_ctx *ctx );
#endif
