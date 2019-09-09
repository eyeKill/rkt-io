#include "spdk_hugetbl.h"

#include "meminfo.h"

#include <spdk/env.h>

void spdk_free_hugetbl(struct spdk_dma_memory *ctx) {
    for (size_t i = 0; i < ctx->nr_allocations; i++) {
      if (ctx->allocations[i]) {
        spdk_dma_free(ctx->allocations[i]);
      }
    }
    free(ctx->allocations);
}

int spdk_alloc_hugetbl(struct spdk_dma_memory *ctx) {
    size_t hugetbl_size = 0;
    const size_t gigabyte = 1024 * 1024 * 1024;
    int r = parse_hugetbl_size(&hugetbl_size);

    if (r < 0) {
        fprintf(stderr, "spdk: Could not get hugetbl information: %s\n", strerror(-r));
        return r;
    }

    if (hugetbl_size < (2 * gigabyte)) {
        fprintf(stderr, "spdk: Less then two gigabyte hugetbl memory found!"
              " Allocate more by writing to /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages\n");
        return -ENOMEM;
    }

    // leave one gigabyte for DPDK
    //size_t gigabytes = (hugetbl_size - gigabyte) / gigabyte;
    // FIXME right now, we get holes in our allocation when allocating more then 16 GB
    size_t gigabytes = 15;

    void** allocations = calloc(gigabytes, sizeof(void*));
    ctx->nr_allocations = gigabytes;
    ctx->allocations = allocations;

    for (size_t i = 0; i < gigabytes; i++) {
        // Allocations bigger then 1GB might fail.
        allocations[i] = spdk_dma_malloc(gigabyte, 0x1000, NULL);
        if (!allocations[i]) {
            fprintf(stderr, "spdk: could not allocate hugetable memory\n");
            goto alloc_failed;
        }
    }

    // useful to debug SPDK mappings
    //char buf[1024];
    //const char* cmd_templ = "cat /proc/%d/maps > /tmp/proc-maps-final.log";
    //snprintf(buf, sizeof(buf), cmd_templ, getpid());
    //system(buf);
    //exit(1);

    return 0;

alloc_failed:
    spdk_free_hugetbl(ctx);
    return -ENOMEM;
}
