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

#ifndef asm
	#define asm __asm
#endif

#define cmpxchg( ptr, _old, _new ) {						\
  volatile uint32_t *__ptr = (volatile uint32_t *)(ptr);	\
  uint32_t __ret;                                   		\
  asm volatile( "lock; cmpxchgl %2,%1"          			\
    : "=a" (__ret), "+m" (*__ptr)               			\
    : "r" (_new), "0" (_old)                    			\
    : "memory");                							\
  );                                            			\
  __ret;                                        			\
}

//#define CAS cmpxchg
#define CAS __sync_bool_compare_and_swap
#define ATOMIC_ADD __sync_add_and_fetch
#define ATOMIC_SUB __sync_sub_and_fetch
#define ATOMIC_SET __sync_lock_test_and_set
#define ATOMIC_RELEASE __sync_lock_release
#define mb __sync_synchronize

#define lmb() asm volatile( "lfence" )
#define smb() asm volatile( "sfence" )

#endif

