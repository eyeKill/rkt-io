#ifndef _SPDK_CONTEXT_H
#define _SPDK_CONTEXT_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

struct spdk_ctrlr_entry {
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_ctrlr_entry *next;
    char name[1024];
};

struct spdk_ns_entry {
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_ns_entry *next;
    struct spdk_nvme_qpair **qpairs;
    size_t qpairs_num;
    int ctl_fd;
};

struct spdk_context {
    struct spdk_ctrlr_entry *controllers;
    struct spdk_ns_entry *namespaces;
    int attach_error;
    pthread_t ctrlr_thread_id;
};

struct spdk_dma_memory  {
  size_t nr_allocations;
  void **allocations;
};

int spdk_initialize(struct spdk_context *ctx, bool primary);

#endif
