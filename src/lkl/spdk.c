#define _BSD_SOURCE
#include <spdk/stdinc.h>

#include <spdk/nvme.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <endian.h>
#include <byteswap.h>
#include <lkl_host.h>

#include "lkl/virtio.h"
#include "lkl/spdk.h"
#include "lkl/virtio_queue.h"
#include "spdk_context.h"

struct lkl_nvme_qpair {
	struct spdk_nvme_qpair	*qpair;
	struct spdk_nvme_ns	*ns;
};

// copied from lkl/tools/lkl/lib/virtio_blk.c
struct virtio_blk_dev {
	struct virtio_dev dev;
	struct lkl_virtio_blk_config config;
	struct lkl_dev_blk_ops *ops;
	struct lkl_disk disk;
};

// copied from lkl/tools/lkl/lib/virtio_blk.c
struct virtio_blk_req_trailer {
	uint8_t status;
};


struct spdk_req {
	struct _virtio_req vio_req;
	void *spdk_buf;
	bool finished;
};

static void spdk_write_completion_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_req *req = (struct spdk_req*) ctx;
	spdk_dma_free(req->spdk_buf);
	// TODO error handling: spdk_nvme_cpl_is_error(cpl)
	virtio_req_complete(&req->vio_req, 0);
	req->finished = true;
	free(req);
}

static int spdk_write(void *spdk_buf,
					  struct virtio_req *vio_req,
					  struct spdk_ns_entry *ns_entry,
					  struct iovec *data,
					  int iov_count,
					  uint64_t lba,
					  uint32_t lba_count)
{
	char *p = (char*) spdk_buf;
	for (int i = 0; i < iov_count; i++) {
		memcpy(p, data[i].iov_base, data[i].iov_len);
		p += data[i].iov_len;
	}
	struct spdk_req *req = (struct spdk_req*) malloc(sizeof(struct spdk_req));
	if (!req) {
		return -ENOMEM;
	}
	memcpy(&req->vio_req, vio_req, sizeof(struct _virtio_req));
	req->spdk_buf = spdk_buf;
	req->finished = false;

	int rc = spdk_nvme_ns_cmd_write(ns_entry->ns,
								ns_entry->qpair, spdk_buf, lba, lba_count,
								spdk_write_completion_cb, req, 0);
	if (rc < 0) {
		free(req);
		spdk_dma_free(spdk_buf);
		return rc;
	}

	// TODO make this asynchronous
	while (!req->finished) {
		spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
	}
	return 0;
};

static void spdk_read_completion_cb(void *ctx, const struct spdk_nvme_cpl *cpl) {
	struct spdk_req *req = (struct spdk_req*) ctx;

	// TODO error handling: spdk_nvme_cpl_is_error(cpl)

	struct iovec *data = &req->vio_req.req.buf[1];
	int iov_count = req->vio_req.req.buf_count - 2;

	char *p = (char*) req->spdk_buf;
	for (int i = 0; i < iov_count; i++) {
		memcpy(data[i].iov_base, p, data[i].iov_len);
		p += data[i].iov_len;
	}

	virtio_req_complete(&req->vio_req, 0);

	spdk_dma_free(req->spdk_buf);

	req->finished = true;
	free(req);
}

static int spdk_read(void *spdk_buf,
					  struct virtio_req *vio_req,
					  struct spdk_ns_entry *ns_entry,
					  struct iovec *data,
					  int iov_count,
					  uint64_t lba,
					  uint32_t lba_count)
{
	struct spdk_req *req = (struct spdk_req*) malloc(sizeof(struct spdk_req));
	if (!req) {
		return -ENOMEM;
	}
	memcpy(&req->vio_req, vio_req, sizeof(struct _virtio_req));
	req->spdk_buf = spdk_buf;
	req->finished = false;

	int rc = spdk_nvme_ns_cmd_read(ns_entry->ns,
								   ns_entry->qpair, spdk_buf, lba, lba_count,
								   spdk_read_completion_cb, req, 0);

	if (rc < 0) {
		free(req);
		spdk_dma_free(spdk_buf);
		return rc;
	}

	// TODO make this asynchronous
	while (!req->finished) {
		spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
	}

	return 0;
};

static int spdk_enqueue(struct virtio_dev *dev, int q, struct virtio_req *req) {
	if (req->buf_count < 3) {
		lkl_printf("virtio_blk: no status buf\n");
		goto err;
	}

	struct lkl_virtio_blk_outhdr *header = req->buf[0].iov_base;
	struct virtio_blk_req_trailer *trailer =
		req->buf[req->buf_count - 1].iov_base;
	struct virtio_blk_dev *blk_dev =
		container_of(dev, struct virtio_blk_dev, dev);

	trailer->status = LKL_DEV_BLK_STATUS_IOERR;

	if (req->buf[0].iov_len != sizeof(*header)) {
		lkl_printf("virtio_blk: bad header buf\n");
		goto err;
	}

	if (req->buf[req->buf_count - 1].iov_len != sizeof(*trailer)) {
		  lkl_printf("virtio_blk: bad status buf\n");
		  goto err;
	}

	unsigned int type = le32toh(header->type);
	unsigned long long sector = le32toh(header->sector);
	struct iovec *buf = &req->buf[1];
	int iov_count = req->buf_count - 2;

	size_t len = 0;
	for (int i = 0; i < iov_count; i++) {
		len += buf[i].iov_len;
	}
	struct spdk_dev *spdk_dev = (struct spdk_dev *)blk_dev->disk.handle;
	struct spdk_ns_entry *ns_entry = &spdk_dev->ns_entry;

	void *spdk_buf = spdk_dma_malloc(len, 0x1000, NULL);
	uint32_t lba_count =
		len / spdk_nvme_ns_get_extended_sector_size(ns_entry->ns);
	uint64_t lba = sector * 512 / lba_count;

	switch (type) {
	case LKL_DEV_BLK_TYPE_READ:
		if (spdk_read(spdk_buf, req, ns_entry, buf, iov_count, lba,
					  lba_count) < 0) {
			goto err;
		};
		break;
	case LKL_DEV_BLK_TYPE_WRITE:
		if (spdk_write(spdk_buf, req, ns_entry, buf, iov_count, lba,
					   lba_count) < 0) {
			goto err;
		};
		break;
	default:
		trailer->status = LKL_DEV_BLK_STATUS_UNSUP;
		goto err;
	}

	trailer->status = LKL_DEV_BLK_STATUS_OK;
	return iov_count;

err:
	virtio_req_complete(req, 0);
	return 0;
}

static int spdk_get_capacity(struct lkl_disk disk, unsigned long long *res)
{
	assert(disk.handle);
	struct spdk_dev *dev = (struct spdk_dev*) disk.handle;
	off_t off = spdk_nvme_ns_get_size(dev->ns_entry.ns);
	*res = off;
	return 0;
}

struct spdk_sync_req {
	void *spdk_buf;
	struct lkl_blk_req *req;
	bool finished;
};

static void spdk_sync_read_completion_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_sync_req *req = (struct spdk_sync_req*) ctx;

	char *p = (char*) req->spdk_buf;
	for (int i = 0; i < req->req->count; i++) {
		memcpy(req->req->buf[i].iov_base, p, req->req->buf[i].iov_len);
		p += req->req->buf[i].iov_len;
	}

	req->finished = true;
}

static int spdk_sync_read(struct spdk_ns_entry *ns_entry,
						  struct spdk_sync_req *req, uint32_t lba,
						  uint32_t lba_count) {
	int rc =
		spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, req->spdk_buf, lba,
							  lba_count, spdk_sync_read_completion_cb, req, 0);
	if (rc != 0) {
		return rc;
	}

	// TODO make this asynchronous
	while (!req->finished) {
		spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
	}

	return rc;
}

static void spdk_sync_completion_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_sync_req *req = (struct spdk_sync_req*) ctx;
	req->finished = true;
}

static int spdk_sync_write(struct spdk_ns_entry *ns_entry,
						   struct spdk_sync_req *req,
						   uint32_t lba,
						   uint32_t lba_count)
{
	char *p = (char*) req->spdk_buf;
	for (int i = 0; i < req->req->count; i++) {
		memcpy(p, req->req->buf[i].iov_base, req->req->buf[i].iov_len);
		p += req->req->buf[i].iov_len;
	}
	int rc = spdk_nvme_ns_cmd_write(ns_entry->ns, ns_entry->qpair,
									req->spdk_buf, lba, lba_count,
									spdk_sync_completion_cb, req, 0);
	if (rc != 0) {
		return rc;
	}

	while (!req->finished) {
		spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
	}

	return rc;
}

static int spdk_sync_flush(struct spdk_ns_entry *ns_entry, struct spdk_sync_req *req)
{
	int rc = spdk_nvme_ns_cmd_flush(ns_entry->ns, ns_entry->qpair, spdk_sync_completion_cb, req);
	if (rc != 0) {
		return rc;
	}

	while (!req->finished) {
		spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
	}

	return rc;
}

static int spdk_sync_request(struct lkl_disk disk, struct lkl_blk_req *req)
{
	size_t len = 0;
	for (int i = 0; i < req->count; i++) {
		len += req->buf[i].iov_len;
	}

	struct spdk_dev *spdk_dev = (struct spdk_dev*) disk.handle;
	struct spdk_ns_entry *ns_entry = &spdk_dev->ns_entry;

	void *spdk_buf = spdk_dma_malloc(len, 0x1000, NULL);
	uint32_t sector_size = spdk_nvme_ns_get_extended_sector_size(ns_entry->ns);
	uint64_t lba = req->sector * 512 / sector_size;
	uint32_t lba_count = len / sector_size;

	struct spdk_sync_req sync_req = {
		.spdk_buf = spdk_buf,
		.req = req,
		.finished = false,
	};

	int rc = LKL_DEV_BLK_STATUS_IOERR;

	switch (req->type) {
	case LKL_DEV_BLK_TYPE_READ:
		if (spdk_sync_read(ns_entry, &sync_req, lba, lba_count) == 0) {
			rc = LKL_DEV_BLK_STATUS_OK;
		}
		break;
	case LKL_DEV_BLK_TYPE_WRITE:
		if (spdk_sync_write(ns_entry, &sync_req, lba, lba_count) == 0) {
			rc = LKL_DEV_BLK_STATUS_OK;
		}
		break;
	case LKL_DEV_BLK_TYPE_FLUSH:
	case LKL_DEV_BLK_TYPE_FLUSH_OUT:
		// when is this actually called?
		if (spdk_sync_flush(ns_entry, &sync_req) == 0) {
			rc = LKL_DEV_BLK_STATUS_OK;
		}
		break;
	default:
		rc = LKL_DEV_BLK_STATUS_UNSUP;
	}

	spdk_dma_free(spdk_buf);
	return rc;
}

static struct lkl_dev_blk_ops spdk_blk_ops = {
	.get_capacity = spdk_get_capacity,
	// For asynchronous I/O we will override enqueue() of the underlying virtio device instead.
	.request = spdk_sync_request,
};

static int spdk_check_features(struct virtio_dev *dev)
{
	if (dev->driver_features == dev->device_features)
		return 0;

	return -LKL_EINVAL;
}

static struct virtio_dev_ops spdk_virtio_ops = {
	.check_features = spdk_check_features,
	//.process_queue = async_virtio_process_queue,
	//.enqueue = spdk_enqueue,
};

static void poll_thread(void *arg)
{
	struct spdk_dev *dev = arg;
	// FIXME one might want to limit the number of completions here,
	// based on benchmarks.
	spdk_nvme_qpair_process_completions(dev->ns_entry.qpair, 0);
	usleep(1);
}

int spdk_env_dpdk_post_init(void);

int sgxlkl_spdk_initialize()
{
	// this function is called internally by SPDK to register pci drivers and
	// initialize address translation. Since we are not running the full
	// initialisation in SPDK, we need to calls this function manually
	return spdk_env_dpdk_post_init();
}

int sgxlkl_register_spdk_device(struct spdk_dev *dev)
{
	assert(dev);
	struct lkl_disk disk = {
		.handle = dev,
		.ops = &spdk_blk_ops,
		.blk_ops = NULL,
		//.blk_ops = &spdk_virtio_ops,
	};
	int dev_id = lkl_disk_add(&disk);
	if (dev_id < 0) {
		fprintf(stderr, "spdk: unable to register disk: %s\n", lkl_strerror(dev_id));
	}
	dev->dev_id = dev_id;
	//dev->poll_tid = lkl_host_ops.thread_create(poll_thread, dev);
	//if (dev->poll_tid == 0) {
	//	fprintf(stderr, "spdk: failed to start spdk poll thread\n");
	//	return -EAGAIN;
	//}
	return dev_id;
}

void final_flush_complete(void* ctx)
{
	bool* ready = (bool*)ctx;
	*ready = true;
}

void sgxlkl_unregister_spdk_device(struct spdk_dev *dev)
{
	// drain also i/o queues here!
	bool ready = false;
	spdk_nvme_ns_cmd_flush(dev->ns_entry.ns, dev->ns_entry.qpair, final_flush_complete, &ready);
	while (!ready) {
		spdk_nvme_qpair_process_completions(dev->ns_entry.qpair, 0);
	}

	if (dev->poll_tid > 0) {
		lkl_host_ops.thread_join(dev->poll_tid);
	}
}
