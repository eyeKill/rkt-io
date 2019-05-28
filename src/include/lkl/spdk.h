#ifndef _MUSLKL_SPDK_H
#define _MUSLKL_SPDK_H

#include <lkl_host.h>
#include <spdk_context.h>

struct spdk_dev {
    // filled out by the caller
    struct spdk_ns_entry ns_entry;
    // set by sgxlkl_register_spdk_device
	lkl_thread_t poll_tid;
    int dev_id;
};

int sgxlkl_spdk_initialize();
int sgxlkl_register_spdk_device(struct spdk_dev *dev);
void sgxlkl_unregister_spdk_device(struct spdk_dev *dev);

#endif
