#ifndef _SPDK_H
#define _SPDK_H

#include <stdbool.h>
#include <pthread.h>

struct spdk_ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_ctrlr_entry	*next;
	char			name[1024];
};
struct spdk_ns_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
	struct spdk_ns_entry		*next;
	struct spdk_nvme_qpair	*qpair;
};

struct spdk_context {
    struct spdk_ctrlr_entry *controllers;
    struct spdk_ns_entry *namespaces;
    int attach_error;
    pthread_t ctrlr_thread_id;
};

int spdk_initialize(struct spdk_context* ctx, bool primary);
void spdk_context_detach(struct spdk_context* ctx);
void spdk_context_free(struct spdk_context* ctx);

#endif
