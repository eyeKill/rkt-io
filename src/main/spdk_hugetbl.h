#ifndef SGXLKL_SPDK_HUGETBL_H
#define SGXLKL_SPDK_HUGETBL_H

#include <spdk_context.h>
#include <sgx_enclave_config.h>

// TODO seem to be the maximum size
#define SPDK_DATA_POOL_MAX_SIZE (1048576 * 2)

// define TODO get the queue depth from
// spdk_nvme_ctrlr_get_default_io_qpair_opts
// and using io_queue_size from spdk_nvme_io_qpair_opts
#define SPDK_QUEUE_DEPTH 1023

int spdk_alloc_dma_memory(struct spdk_dma_memory *ctx);
void spdk_free_dma_memory(struct spdk_dma_memory *ctx);
int dpdk_allocate_dma_memory(struct enclave_dpdk_dma_memory *mem);


#endif /* SGXLKL_SPDK_HUGETBL_H */
