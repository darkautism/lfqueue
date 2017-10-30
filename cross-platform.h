#ifndef __CROSS_PLATFORM_H__
#define __CROSS_PLATFORM_H__
// bool define
#ifdef __KERNEL__
	#include <sys/stdbool.h>
#else
	#include <stdbool.h>
#endif

// malloc free
#ifdef __KERNEL__
        #define malloc(x) kmalloc(x, GFP_KERNEL )
	#define free kfree
	#define calloc(x,y) kmalloc(x*y, GFP_KERNEL | __GFP_ZERO )
	#include<linux/string.h>
#else
	#include <stdlib.h>
	#include <string.h>
#endif

#define CAS __sync_bool_compare_and_swap
#define ATOMIC_ADD __sync_add_and_fetch
#define ATOMIC_SUB __sync_sub_and_fetch

#endif

