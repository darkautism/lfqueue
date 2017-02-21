# lfqueue
[![Build Status](https://travis-ci.org/darkautism/lfqueue.svg?branch=master)](https://travis-ci.org/darkautism/lfqueue)
Minimize lock-free queue, it's easy to use and easy to read. It's only 50 line code so it is easy to understand, best code for education ever!

Support multiple comsumer and multiple producer at sametime.

## How to use

Just copy past everywhere and use it. If you copy these code into your project, you must use -O0 flag to compile.

## Example

It is an minimize sample code for how to using lfq.

	``` c
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
	```
	
## License

WTFPL