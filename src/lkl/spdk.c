#define _BSD_SOURCE
#include <spdk/stdinc.h>

#include <spdk/nvme.h>

#include <assert.h>
#include <byteswap.h>
#include <endian.h>
#include <errno.h>
#include <linux/spdk.h>
#include <lkl_host.h>
#include <stdio.h>
#include <spdk_bench.h>

#include "lkl/spdk.h"
#include "lkl/virtio.h"
#include "lkl/virtio_queue.h"
#include "lthread_int.h"
#include "spdk_context.h"

unsigned long spdk_dma_memory_start = 0;
unsigned long spdk_dma_memory_end = 0;

int spdk_env_dpdk_post_init(void);

int sgxlkl_spdk_initialize() {
    // this function is called internally by SPDK to register pci drivers and
    // initialize address translation. Since we are not running the full
    // initialisation in SPDK, we need to calls this function manually
    return spdk_env_dpdk_post_init();
}

// used in the spdk kernel module poll thread
void spdk_yield_thread() {
    _lthread_yield_cb(lthread_self(), __scheduler_enqueue, lthread_self());
}

void *sgxlkl_spdk_malloc(size_t size) {
    return spdk_dma_malloc(size, 0x1000, NULL);
}

void sgxlkl_spdk_free(void *ptr) {
    spdk_dma_free(ptr);
}

int sgxlkl_register_spdk_device(struct spdk_dev *dev) {
    struct lkl_ifreq ifr;
    int fd, err;

    // Uncomment for benchmarking
    //run_spdk_bench(&dev->ns_entry);

    fd = lkl_sys_open("/dev/spdk-control", LKL_O_RDONLY, 0);

    if (fd < 0) {
        fprintf(stderr, "spdk: cannot open /dev/spdk-control: %s\n",
                lkl_strerror(-fd));
        return fd;
    }
    dev->ns_entry.ctl_fd = fd;

    err = lkl_sys_ioctl(fd, SPDK_CTL_ADD, (long)&dev->ns_entry);

    if (err < 0) {
        lkl_sys_close(fd);
        fprintf(stderr, "spdk: ioctl SPDK_CTL_ADD failed: %s\n",
                lkl_strerror(-fd));
    }

    return err;
}

void final_flush_complete(void *ctx) {
    bool *ready = (bool *)ctx;
    *ready = true;
}

void sgxlkl_unregister_spdk_device(struct spdk_dev *dev) {
    // drain also i/o queues here!
    bool ready;
    struct spdk_nvme_qpair *qpair;

    close(dev->ns_entry.ctl_fd);

    for (int i = 0; i < dev->ns_entry.qpairs_num; i++) {
        qpair = dev->ns_entry.qpairs[i];
        if (qpair) {
            ready = false;
            spdk_nvme_ns_cmd_flush(dev->ns_entry.ns, qpair,
                                   final_flush_complete, &ready);
            while (!ready) {
                spdk_nvme_qpair_process_completions(qpair, 0);
            }
        }
    }
}

void sgxlkl_register_spdk_dma_memory(struct spdk_dma_memory* ctx) {
    void* lowest_address = (void*) -1;
    void* highest_address = (void*) 0;
    const size_t gigabyte = 1024 * 1024 * 1024;

    for (size_t i = 0; i < ctx->nr_allocations; i++) {
        // Allocations bigger then 1GB might fail.
        if (ctx->allocations[i] < lowest_address) {
            lowest_address = ctx->allocations[i];
        }
        if ((ctx->allocations[i] + gigabyte) > highest_address) {
            highest_address = ctx->allocations[i] + gigabyte;
        }
    }

    spdk_dma_memory_start = lowest_address;
    spdk_dma_memory_end = highest_address;
}
