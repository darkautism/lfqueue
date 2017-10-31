#ifndef __LFQ_H__
#define __LFQ_H__

struct lfq_node{
	void * data;
	struct lfq_node volatile * next;
	int can_free;
	struct lfq_node * free_next;
};

#define MAXHPSIZE 256
#define MAXFREE 10

struct lfq_ctx{
	struct lfq_node volatile * head;
	struct lfq_node * tail;
	int volatile count;
	struct lfq_node volatile * HP[MAXHPSIZE];
	int volatile tid_map[MAXHPSIZE];
	int volatile is_freeing;
	struct lfq_node * fph; // free pool head
	struct lfq_node * fpt; // free pool tail
};

int lfq_init(struct lfq_ctx *ctx);
int lfq_clean(struct lfq_ctx *ctx);
int lfq_enqueue(struct lfq_ctx *ctx, void * data);
void * lfq_dequeue(struct lfq_ctx *ctx );
#endif
