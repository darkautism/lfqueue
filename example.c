#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "lfq.h"

int main(void) {
    struct lfq_ctx ctx;

    if (lfq_init(&ctx, 0) != 0) {
        fprintf(stderr, "lfq_init failed\n");
        return 1;
    }

    const uintptr_t values[] = {1, 3, 5, 8, 4, 6};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (lfq_enqueue(&ctx, (void *)values[i]) != 0) {
            fprintf(stderr, "lfq_enqueue failed\n");
            lfq_clean(&ctx);
            return 1;
        }
    }

    for (;;) {
        void *item = lfq_dequeue(&ctx);
        if (item == NULL)
            break;
        if (item == LFQ_ERROR) {
            fprintf(stderr, "lfq_dequeue failed\n");
            lfq_clean(&ctx);
            return 1;
        }
        printf("lfq_dequeue %" PRIuPTR "\n", (uintptr_t)item);
    }

    return lfq_clean(&ctx) == 0 ? 0 : 1;
}
