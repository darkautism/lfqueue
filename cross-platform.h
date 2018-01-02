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
#define ATOMIC_SET __sync_lock_test_and_set
#define ATOMIC_RELEASE __sync_lock_release

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__
	#define ATOMIC_SUB __sync_sub_and_fetch
	#define ATOMIC_SUB64 ATOMIC_SUB
	#define CAS __sync_bool_compare_and_swap
	#define ATOMIC_ADD __sync_add_and_fetch
	#define ATOMIC_ADD64 ATOMIC_ADD
	#define mb __sync_synchronize
	#define lmb() asm volatile( "lfence" )
	#define smb() asm volatile( "sfence" )

	// thread
	#define THREAD_WAIT(x) pthread_join(x, NULL);
	#define THREAD_ID pthread_self
	#define THREAD_FN void *
	#define THREAD_YIELD pthread_yield
	#define THREAD_TOKEN pthread_t

#else
	#include <Windows.h>	
	#define ATOMIC_SUB(x,y) InterlockedExchangeAddNoFence(x, -y)
	#define ATOMIC_SUB64(x,y) InterlockedExchangeAddNoFence64(x, -y)
	#define ATOMIC_ADD InterlockedExchangeAddNoFence
	#define ATOMIC_ADD64 InterlockedExchangeAddNoFence64
	#ifdef _WIN64
		#define mb() MemoryBarrier()
		#define lmb() LoadFence()
		#define smb() StoreFence()
		inline bool __CAS(LONG64 volatile *x, LONG64 y, LONG64 z) {
			return InterlockedCompareExchangeNoFence64(x, z, y) == y;
		}
		#define CAS(x,y,z)  __CAS((LONG64 volatile *)x, (LONG64)y, (LONG64)z)
	#else
		#define mb() asm mfence
		#define lmb() asm lfence
		#define smb() asm sfence
		inline bool __CAS(LONG volatile *x, LONG y, LONG z) {
			return InterlockedCompareExchangeNoFence(x, z, y) == y;
		}
		#define CAS(x,y,z)  __CAS((LONG volatile *)x, (LONG)y, (LONG)z)
	#endif

	// thread

	#define THREAD_WAIT(x) WaitForSingleObject(x, INFINITE);
	#define THREAD_ID GetCurrentThreadId
	#define THREAD_FN WORD WINAPI
	#define THREAD_YIELD SwitchToThread
	#define THREAD_TOKEN HANDLE
#endif
#endif

