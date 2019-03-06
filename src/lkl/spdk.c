#include "spdk/stdinc.h"

#include "spdk/nvme.h"

#include "lkl_spdk.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "lkl/virtio.h"


struct lkl_nvme_qpair {
	struct spdk_nvme_qpair	*qpair;
	struct spdk_nvme_ns	*ns;
};

static int spdk_enqueue(struct virtio_dev *dev, int q, struct virtio_req *req)
{
	return -EIO;
}

static int spdk_get_capacity(struct lkl_disk disk, unsigned long long *res)
{
	assert(disk.handle);
	struct lkl_spdk_ns_entry *ns_entry = (struct lkl_spdk_ns_entry*) disk.handle;
	off_t off = spdk_nvme_ns_get_size(ns_entry->ns);
	*res = off;
	return 0;
}

static struct lkl_dev_blk_ops spdk_blk_ops = {
	.get_capacity = spdk_get_capacity,
	// We will override enqueue() of the underlying virtio device instead.
	.request = NULL,
};

int sgxlkl_register_spdk_context(struct lkl_spdk_context *ctx) {
	struct lkl_spdk_ns_entry *ns_entry = ctx->namespaces;
	while (ns_entry) {
		struct lkl_spdk_ns_entry *next = ns_entry->next;
		assert(ns_entry->qpair);
		struct lkl_disk disk = { .handle = ns_entry, .ops = &spdk_blk_ops, };
		int disk_dev_id = lkl_disk_add(&disk);
		if (disk_dev_id < 0) {
			fprintf(stderr, "spdk: unable to register spdk disk: %s\n", lkl_strerror(disk_dev_id));
			return disk_dev_id;
		}
		// struct virtio_dev is embedded into struct virtio_blk_dev
		struct virtio_dev *dev = (struct virtio_dev*)disk.dev;
		dev->ops->enqueue = spdk_enqueue;
		ns_entry = next;
	}
    return 0;
}
