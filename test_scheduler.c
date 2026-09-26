#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LFQ_TEST_HOOKS 1
#include "lfq.h"

#define CHECK(expr) do {     if (!(expr)) {         fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);         exit(1);     } } while (0)

struct gate {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    enum lfq_test_hook_point point;
    int hit;
    int release;
};

static int first_allowed_cpu(void) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CHECK(sched_getaffinity(0, sizeof(set), &set) == 0);
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        if (CPU_ISSET(cpu, &set))
            return cpu;
    return -1;
}

static int second_allowed_cpu(int first) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CHECK(sched_getaffinity(0, sizeof(set), &set) == 0);
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        if (cpu != first && CPU_ISSET(cpu, &set))
            return cpu;
    return first;
}

static void pin_current(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    CHECK(pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0);
}

static void gate_init(struct gate *g, enum lfq_test_hook_point point) {
    CHECK(pthread_mutex_init(&g->mu, NULL) == 0);
    CHECK(pthread_cond_init(&g->cv, NULL) == 0);
    g->point = point;
    g->hit = 0;
    g->release = 0;
}

static void gate_destroy(struct gate *g) {
    CHECK(pthread_cond_destroy(&g->cv) == 0);
    CHECK(pthread_mutex_destroy(&g->mu) == 0);
}

static void gate_wait_hit(struct gate *g) {
    struct timespec deadline;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;

    CHECK(pthread_mutex_lock(&g->mu) == 0);
    while (!g->hit) {
        int rc = pthread_cond_timedwait(&g->cv, &g->mu, &deadline);
        if (rc == ETIMEDOUT) {
            fprintf(stderr, "timed out waiting for hook %d\n", (int)g->point);
            exit(2);
        }
        CHECK(rc == 0);
    }
    CHECK(pthread_mutex_unlock(&g->mu) == 0);
}

static void gate_release(struct gate *g) {
    CHECK(pthread_mutex_lock(&g->mu) == 0);
    g->release = 1;
    CHECK(pthread_cond_broadcast(&g->cv) == 0);
    CHECK(pthread_mutex_unlock(&g->mu) == 0);
}

static void scheduler_hook(enum lfq_test_hook_point point,
                           struct lfq_ctx *ctx,
                           struct lfq_node *first,
                           struct lfq_node *second,
                           void *arg) {
    (void)ctx;
    (void)first;
    (void)second;
    struct gate *g = (struct gate *)arg;

    CHECK(pthread_mutex_lock(&g->mu) == 0);
    if (point == g->point && !g->hit) {
        g->hit = 1;
        CHECK(pthread_cond_broadcast(&g->cv) == 0);
        while (!g->release)
            CHECK(pthread_cond_wait(&g->cv, &g->mu) == 0);
    }
    CHECK(pthread_mutex_unlock(&g->mu) == 0);
}

struct enq_args {
    struct lfq_ctx *ctx;
    uintptr_t value;
    int cpu;
    int rc;
};

static void *enqueue_thread(void *raw) {
    struct enq_args *a = (struct enq_args *)raw;
    pin_current(a->cpu);
    a->rc = lfq_enqueue(a->ctx, (void *)a->value);
    return NULL;
}

struct deq_args {
    struct lfq_ctx *ctx;
    int tid;
    int cpu;
    int rc;
    void *out;
};

static void *dequeue_thread(void *raw) {
    struct deq_args *a = (struct deq_args *)raw;
    pin_current(a->cpu);
    a->out = NULL;
    a->rc = lfq_try_dequeue_tid(a->ctx, a->tid, &a->out);
    return NULL;
}

struct clean_args {
    struct lfq_ctx *ctx;
    int cpu;
    int rc;
};

static void *clean_thread(void *raw) {
    struct clean_args *a = (struct clean_args *)raw;
    pin_current(a->cpu);
    a->rc = lfq_clean(a->ctx);
    return NULL;
}

static void test_enqueue_completion_visible(int cpu) {
    struct lfq_ctx ctx;
    struct gate gate;
    pthread_t p1;
    void *out = NULL;

    CHECK(lfq_init(&ctx, 4) == 0);
    gate_init(&gate, LFQ_TEST_ENQ_AFTER_PROTECT_TAIL);
    lfq_test_set_hook(scheduler_hook, &gate);

    struct enq_args a = {&ctx, 1, cpu, -999};
    CHECK(pthread_create(&p1, NULL, enqueue_thread, &a) == 0);
    gate_wait_hit(&gate);

    /* P1 is parked with the old tail protected. P2 must still complete. */
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)2) == 0);

    /* A completed P2 enqueue must already be visible to a later consumer. */
    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 1);
    CHECK((uintptr_t)out == 2);

    gate_release(&gate);
    CHECK(pthread_join(p1, NULL) == 0);
    CHECK(a.rc == 0);

    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 1);
    CHECK((uintptr_t)out == 1);
    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 0);

    lfq_test_set_hook(NULL, NULL);
    gate_destroy(&gate);
    CHECK(lfq_clean(&ctx) == 0);
    printf("schedule: completed enqueue visibility PASS\n");
}

static void test_consumer_hazard_survives_reclaim(int cpu) {
    struct lfq_ctx ctx;
    struct gate gate;
    pthread_t c1;
    void *out = NULL;

    CHECK(lfq_init(&ctx, 4) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)11) == 0);
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)22) == 0);

    gate_init(&gate, LFQ_TEST_DEQ_AFTER_PROTECT_HEAD);
    lfq_test_set_hook(scheduler_hook, &gate);

    struct deq_args a = {&ctx, 0, cpu, -999, NULL};
    CHECK(pthread_create(&c1, NULL, dequeue_thread, &a) == 0);
    gate_wait_hit(&gate);

    /* C1 protects the dummy head. C2 removes it and retires it. */
    CHECK(lfq_try_dequeue_tid(&ctx, 1, &out) == 1);
    CHECK((uintptr_t)out == 11);

    /* A forced scan must not reclaim C1's protected old head. */
    lfq_test_force_reclaim(&ctx);
    CHECK(lfg_count_freelist(&ctx) >= 1);

    gate_release(&gate);
    CHECK(pthread_join(c1, NULL) == 0);
    CHECK(a.rc == 1);
    CHECK((uintptr_t)a.out == 22);

    lfq_test_set_hook(NULL, NULL);
    gate_destroy(&gate);

    lfq_test_force_reclaim(&ctx);
    CHECK(lfq_clean(&ctx) == 0);
    printf("schedule: consumer hazard vs reclaim PASS\n");
}

static void test_producer_stale_tail_survives_reclaim(int cpu) {
    struct lfq_ctx ctx;
    struct gate gate;
    pthread_t p1;
    void *out = NULL;

    CHECK(lfq_init(&ctx, 4) == 0);

    gate_init(&gate, LFQ_TEST_ENQ_AFTER_PROTECT_TAIL);
    lfq_test_set_hook(scheduler_hook, &gate);

    struct enq_args a = {&ctx, 101, cpu, -999};
    CHECK(pthread_create(&p1, NULL, enqueue_thread, &a) == 0);
    gate_wait_hit(&gate);

    /* P1 holds a stale local dummy tail. Let P2 advance the queue. */
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)202) == 0);
    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 1);
    CHECK((uintptr_t)out == 202);

    /* The retired dummy must remain alive because P1 still hazards it. */
    lfq_test_force_reclaim(&ctx);
    CHECK(lfg_count_freelist(&ctx) >= 1);

    gate_release(&gate);
    CHECK(pthread_join(p1, NULL) == 0);
    CHECK(a.rc == 0);

    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 1);
    CHECK((uintptr_t)out == 101);

    lfq_test_set_hook(NULL, NULL);
    gate_destroy(&gate);

    lfq_test_force_reclaim(&ctx);
    CHECK(lfq_clean(&ctx) == 0);
    printf("schedule: producer stale-tail hazard PASS\n");
}

static void test_cleanup_rejects_active_operation(int cpu) {
    struct lfq_ctx ctx;
    struct gate gate;
    pthread_t p1;
    void *out = NULL;

    CHECK(lfq_init(&ctx, 2) == 0);
    gate_init(&gate, LFQ_TEST_ENQ_AFTER_PROTECT_TAIL);
    lfq_test_set_hook(scheduler_hook, &gate);

    struct enq_args a = {&ctx, 31337, cpu, -999};
    CHECK(pthread_create(&p1, NULL, enqueue_thread, &a) == 0);
    gate_wait_hit(&gate);

    CHECK(lfq_clean(&ctx) == -EBUSY);

    gate_release(&gate);
    CHECK(pthread_join(p1, NULL) == 0);
    CHECK(a.rc == 0);

    CHECK(lfq_try_dequeue_tid(&ctx, 0, &out) == 1);
    CHECK((uintptr_t)out == 31337);

    lfq_test_set_hook(NULL, NULL);
    gate_destroy(&gate);
    CHECK(lfq_clean(&ctx) == 0);
    printf("schedule: cleanup vs active operation PASS\n");
}

static void test_clean_gate_blocks_new_operations(int cpu) {
    struct lfq_ctx ctx;
    struct gate gate;
    pthread_t cleaner;
    void *out = NULL;

    CHECK(lfq_init(&ctx, 2) == 0);
    gate_init(&gate, LFQ_TEST_CLEAN_AFTER_GATE);
    lfq_test_set_hook(scheduler_hook, &gate);

    struct clean_args a = {&ctx, cpu, -999};
    CHECK(pthread_create(&cleaner, NULL, clean_thread, &a) == 0);
    gate_wait_hit(&gate);

    /* Cleanup owns the lifecycle gate, so no operation may enter now. */
    CHECK(lfq_enqueue(&ctx, (void *)(uintptr_t)1) == -EBUSY);
    CHECK(lfq_try_dequeue(&ctx, &out) == -EBUSY);

    gate_release(&gate);
    CHECK(pthread_join(cleaner, NULL) == 0);
    CHECK(a.rc == 0);

    lfq_test_set_hook(NULL, NULL);
    gate_destroy(&gate);
    printf("schedule: cleanup gate blocks new operations PASS\n");
}

int main(void) {
    int cpu0 = first_allowed_cpu();
    int cpu1 = second_allowed_cpu(cpu0);

    /* Pin main and all deterministic actors to one CPU: real OS preemption. */
    pin_current(cpu0);
    printf("deterministic scheduler tests: same_cpu=%d alternate_cpu=%d\n", cpu0, cpu1);

    test_enqueue_completion_visible(cpu0);
    test_consumer_hazard_survives_reclaim(cpu0);
    test_producer_stale_tail_survives_reclaim(cpu0);
    test_cleanup_rejects_active_operation(cpu0);
    test_clean_gate_blocks_new_operations(cpu0);

    printf("deterministic scheduler tests PASS\n");
    return 0;
}
