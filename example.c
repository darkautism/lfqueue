#include <stdio.h>
#include <stdlib.h>
#include "lfq.h"

int main() {
	long ret;
	struct lfq_ctx ctx;
	lfq_init(&ctx);
	lfq_enqueue(&ctx,(void *)1);
	lfq_enqueue(&ctx,(void *)3);
	lfq_enqueue(&ctx,(void *)5);
	lfq_enqueue(&ctx,(void *)8);
	lfq_enqueue(&ctx,(void *)4);
	lfq_enqueue(&ctx,(void *)6);
	
	while ( (ret = (long)lfq_dequeue(&ctx)) != 0 )
		printf("lfq_dequeue %ld\n", ret);
	
	lfq_clean(&ctx);
	return 0;
}