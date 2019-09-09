#ifndef SGXLKL_SPDK_HUGETBL_H
#define SGXLKL_SPDK_HUGETBL_H

#include <spdk_context.h>

int spdk_alloc_hugetbl(struct spdk_dma_memory *ctx);
void spdk_free_hugetbl(struct spdk_dma_memory *ctx);

#endif /* SGXLKL_SPDK_HUGETBL_H */
