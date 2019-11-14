#ifndef SGXLKL_SPDK_HUGETBL_H
#define SGXLKL_SPDK_HUGETBL_H

#include <spdk_context.h>
#include <sgx_enclave_config.h>

int spdk_alloc_hugetbl(struct spdk_dma_memory *ctx);
void spdk_free_hugetbl(struct spdk_dma_memory *ctx);
int dpdk_allocate_dma_memory(struct enclave_dpdk_dma_memory *mem);

#endif /* SGXLKL_SPDK_HUGETBL_H */
