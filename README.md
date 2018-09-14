# lfqueue [![Build Status](https://travis-ci.org/darkautism/lfqueue.svg?branch=HP)](https://travis-ci.org/darkautism/lfqueue)

Minimize lock-free queue, it's easy to use and easy to read. It's only 150 line code so it is easy to understand, best code for education ever!

***Do not use these code in production***

Support multiple comsumer and multiple producer at sametime.

| Arch              | Build status | Test         |
| ----------------- | ------------ | ------------ |
| Linux             | [![Build Status](https://travis-ci.org/darkautism/lfqueue.svg?branch=HP)](https://travis-ci.org/darkautism/lfqueue)| On testing |
| Windows(msbuild)  | [![Build status](https://ci.appveyor.com/api/projects/status/yu04l5atf0j259kd?svg=true)](https://ci.appveyor.com/project/darkautism/lfqueue-2puqk) | Not tested [Issue](#free-memory-very-slow-in-visual-studio) |
| Windows(Mingw)    | [![Build status](https://ci.appveyor.com/api/projects/status/4nng8ye801ycyvgn/branch/HP?svg=true)](https://ci.appveyor.com/project/darkautism/lfqueue/branch/HP) | Not tested |
| Windows(MinGW64)  | [![Build status](https://ci.appveyor.com/api/projects/status/4457r35yh2x4f52d/branch/HP?svg=true)](https://ci.appveyor.com/project/darkautism/lfqueue-4jybw/branch/HP) | Not tested |
| Windows(Cygwin)   | [![Build status](https://ci.appveyor.com/api/projects/status/xb9oww8jtbaxa9so/branch/HP?svg=true)](https://ci.appveyor.com/project/darkautism/lfqueue-7hmwx/branch/HP) | Not tested |
| Windows(Cygwin64) | [![Build status](https://ci.appveyor.com/api/projects/status/qjltyv4j963s86xd/branch/HP?svg=true)](https://ci.appveyor.com/project/darkautism/lfqueue-wepul/branch/HP) | Not tested |

## Build Guide

In any gnu toolchain, just type `make`.

In any visual studio build toolchain, just type `msbuild "Visual Stdio\lfqueue.sln" /verbosity:minimal /property:Configuration=Release /property:Platform=x86` or 64bit version `msbuild "Visual Stdio\lfqueue.sln" /verbosity:minimal /property:Configuration=Release /property:Platform=x64`.


## How to use

Just copy past everywhere and use it. If you copy these code into your project.

## Next Milestone

- Compile on MACOS ( **I do not have MAC, need somebody help!!** )
- Compile in kernel module.
- Use lock-free memory manager. (free is very slow in windows)

## Example

### Sample example

It is an minimize sample code for how to using lfq.

**Even if int or long value is valid input data, but you will hard to distinguish empty queue or other error message.**

**We not suggestion use int or long as queue data type**

``` c
#include <stdio.h>
#include <stdlib.h>
#include "lfq.h"

int main() {
	long ret;
	struct lfq_ctx ctx;
	lfq_init(&ctx, 0);
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

### Advance example

If you want to get best performance with the cost of developing speed, you can control thread_id yourself.

**This API only impilement on HP branch**

``` c
#include <stdio.h>
#include <stdlib.h>
#include "lfq.h"

#define MAX_CONSUMER_THREAD 4

int main() {
	long ret;
	struct lfq_ctx ctx;
	lfq_init(&ctx, MAX_CONSUMER_THREAD);
	lfq_enqueue(&ctx,(void *)1);
	
	// The second number is thread id, this thread id should unique between threads.
	// And this tid must less than MAX_CONSUMER_THREAD
	// In this sample code, this tid must 0, 1, 2, 3.
	ret = (long)lfq_dequeue_tid(&ctx, 1);

	lfq_clean(&ctx);
	return 0;
}
```

## API

### lfq_init(struct lfq_ctx *ctx, int max_consume_thread)

Init lock-free queue.

**Arguments:**
- **ctx** : Lock-free queue handler.
- **max_consume_thread** : Max consume thread numbers. If this value set to zero, use default value (16).

**Return:** The lfq_init() functions return zero on success. On error, this functions return negative errno.


### lfq_clean(struct lfq_ctx *ctx)

Clean lock-free queue from ctx.

**Arguments:**
- **ctx** : Lock-free queue handler.

**Return:** The lfq_clean() functions return zero on success. On error, this functions return -1.


### lfq_enqueue(struct lfq_ctx *ctx, void * data)

Push data into queue.

**Arguments:**
- **ctx** : Lock-free queue handler.
- **data** : User data.

**Return:** The lfq_clean() functions return zero on success. On error, this functions return negative errno.


### lfq_dequeue(struct lfq_ctx *ctx)

Pop data from queue.

**Arguments:**
- **ctx** : Lock-free queue handler.

**Return:** The lfq_clean() functions return zero if empty queue. Return positive pointer.  On error, this functions return negative errno.

### lfq_dequeue_tid(struct lfq_ctx *ctx, int tid)

Pop data from queue.

**Arguments:**
- **ctx** : Lock-free queue handler.
- **tid** : Unique thread id.

**Return:** The lfq_dequeue_tid() functions return zero if empty queue. Return positive pointer.  On error, this functions return negative errno.

## Issues

### ENOMEM

This lfqueue do not have size limit, so count your struct yourself.

### Can i iterate lfqueue inner struct?

No, iterate inner struct is not threadsafe.

If you do not get segmentation fault because you are iterate lfqueue in single thread.

We should always iterate lfqueue by `lfq_enqueue` and `lfq_dequeue` method.

### CPU time slice waste? Other thread blocking and waiting for swap?

**Enqueue**

No, CAS operator not lock. Loser not block and wait winner.

If a thread race win a CAS, other thread will get false and try to retrive next pointer. Because winner already swap tail, so other losers can do next race.

**Example:**
```
4 thread race push
1 win Push A, 2 lose, 1 do not have CPU time slice
1 win go out queue, 2 losers race A as tail, 1 race initnode as tail
1 win go out queue, 1 win push B, 1 lose, 1 race A failed because tail not initnode now
```

So lock-free queue have better performance then lock queue.

**Dequeue**

We choice Hazard Pointers to reaolve ABA problems. So dequeue is fast as enqueue.

There has many papers to resolve this problem:

- Lock-Free Reference Counting
- ABA-Prevention Tags
- [Hazard Pointers](https://github.com/darkautism/lfqueue/tree/HP) **Beta**
	- [Algorithm Paper](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.395.378&rep=rep1&type=pdf)
- [Fucking stupid head wait](https://github.com/darkautism/lfqueue/tree/FSHW) **Stable**

### Free memory very slow in Visual studio

Sorry, i have no idea why. Still finding problems in windows.

## Contributions

[pcordes](https://github.com/pcordes)

## License

WTFPL
