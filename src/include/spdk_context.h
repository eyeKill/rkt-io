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
    char* key;
    size_t key_len;
    int attach_error;
    pthread_t ctrlr_thread_id;
};

struct spdk_dma_memory  {
  size_t nr_allocations;
  void **allocations;
  struct spdk_mempool *data_pool;
  size_t data_pool_size;
};

int spdk_initialize(struct spdk_context *ctx, bool primary);
void spdk_context_detach(struct spdk_context *ctx);
void spdk_context_free(struct spdk_context *ctx);


#endif
