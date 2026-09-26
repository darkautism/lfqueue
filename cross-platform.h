#ifndef __CROSS_PLATFORM_H__
#define __CROSS_PLATFORM_H__

#ifdef __KERNEL__
#include <sys/stdbool.h>
#include <linux/types.h>
#define malloc(x) kmalloc((x), GFP_KERNEL)
#define free kfree
#define calloc(x,y) kmalloc((x) * (y), GFP_KERNEL | __GFP_ZERO)
#include <linux/string.h>
typedef int lfq_atomic_int_t;
#else
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#endif

#if defined(_MSC_VER)

#include <Windows.h>
typedef volatile LONG lfq_atomic_int_t;

static __forceinline void *lfq_atomic_load_ptr(void * volatile *ptr) {
    return InterlockedCompareExchangePointer((PVOID volatile *)ptr, NULL, NULL);
}

static __forceinline void lfq_atomic_store_ptr(void * volatile *ptr, void *value) {
    (void)InterlockedExchangePointer((PVOID volatile *)ptr, value);
}

static __forceinline void *lfq_atomic_exchange_ptr(void * volatile *ptr, void *value) {
    return InterlockedExchangePointer((PVOID volatile *)ptr, value);
}

static __forceinline bool lfq_atomic_cas_ptr(void * volatile *ptr, void **expected, void *desired) {
    void *actual = InterlockedCompareExchangePointer((PVOID volatile *)ptr, desired, *expected);
    if (actual == *expected)
        return true;
    *expected = actual;
    return false;
}

static __forceinline LONG lfq_atomic_load_int(lfq_atomic_int_t *ptr) {
    return InterlockedCompareExchange((volatile LONG *)ptr, 0, 0);
}

static __forceinline void lfq_atomic_store_int(lfq_atomic_int_t *ptr, LONG value) {
    (void)InterlockedExchange((volatile LONG *)ptr, value);
}

static __forceinline bool lfq_atomic_cas_int(lfq_atomic_int_t *ptr, LONG *expected, LONG desired) {
    LONG actual = InterlockedCompareExchange((volatile LONG *)ptr, desired, *expected);
    if (actual == *expected)
        return true;
    *expected = actual;
    return false;
}

static __forceinline LONG lfq_atomic_fetch_add_int(lfq_atomic_int_t *ptr, LONG value) {
    return InterlockedExchangeAdd((volatile LONG *)ptr, value);
}

#define LFQ_ALIGNAS(n) __declspec(align(n))
#define mb() MemoryBarrier()
#define lmb() MemoryBarrier()
#define smb() MemoryBarrier()

#define ATOMIC_ADD(ptr, value) (InterlockedAdd((volatile LONG *)(ptr), (LONG)(value)))
#define ATOMIC_SUB(ptr, value) (InterlockedAdd((volatile LONG *)(ptr), -(LONG)(value)))
#define ATOMIC_ADD64(ptr, value) (InterlockedAdd64((volatile LONG64 *)(ptr), (LONG64)(value)))
#define ATOMIC_SUB64(ptr, value) (InterlockedAdd64((volatile LONG64 *)(ptr), -(LONG64)(value)))

static __forceinline void lfq_thread_wait(HANDLE handle) {
    if (handle) {
        WaitForSingleObject(handle, INFINITE);
        CloseHandle(handle);
    }
}

#define THREAD_WAIT(x) lfq_thread_wait(x)
#define THREAD_ID() GetCurrentThreadId()
#define THREAD_FN DWORD WINAPI
#define THREAD_YIELD() SwitchToThread()
#define THREAD_TOKEN HANDLE

#else

#include <pthread.h>
#include <sched.h>
typedef volatile int lfq_atomic_int_t;

static inline void *lfq_atomic_load_ptr(void * volatile *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void lfq_atomic_store_ptr(void * volatile *ptr, void *value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

static inline void *lfq_atomic_exchange_ptr(void * volatile *ptr, void *value) {
    return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
}

static inline bool lfq_atomic_cas_ptr(void * volatile *ptr, void **expected, void *desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int lfq_atomic_load_int(lfq_atomic_int_t *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void lfq_atomic_store_int(lfq_atomic_int_t *ptr, int value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

static inline bool lfq_atomic_cas_int(lfq_atomic_int_t *ptr, int *expected, int desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int lfq_atomic_fetch_add_int(lfq_atomic_int_t *ptr, int value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

#define LFQ_ALIGNAS(n) __attribute__((aligned(n)))
#define mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define lmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smb() __atomic_thread_fence(__ATOMIC_RELEASE)

#define ATOMIC_ADD(ptr, value) __sync_add_and_fetch((ptr), (value))
#define ATOMIC_SUB(ptr, value) __sync_sub_and_fetch((ptr), (value))
#define ATOMIC_ADD64(ptr, value) __sync_add_and_fetch((ptr), (value))
#define ATOMIC_SUB64(ptr, value) __sync_sub_and_fetch((ptr), (value))

#define THREAD_WAIT(x) pthread_join((x), NULL)
#define THREAD_ID() ((unsigned long)pthread_self())
#define THREAD_FN void *
#define THREAD_YIELD() sched_yield()
#define THREAD_TOKEN pthread_t

#endif

#endif
