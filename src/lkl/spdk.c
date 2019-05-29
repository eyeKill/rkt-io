#define _BSD_SOURCE
#include <spdk/stdinc.h>

#include <spdk/nvme.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <endian.h>
#include <byteswap.h>
#include <lkl_host.h>
#include <linux/spdk.h>

#include "lthread_int.h"
#include "lkl/virtio.h"
#include "lkl/spdk.h"
#include "lkl/virtio_queue.h"
#include "spdk_context.h"

int spdk_env_dpdk_post_init(void);

int sgxlkl_spdk_initialize()
{
	// this function is called internally by SPDK to register pci drivers and
	// initialize address translation. Since we are not running the full
	// initialisation in SPDK, we need to calls this function manually
	return spdk_env_dpdk_post_init();
}

// used in the spdk kernel module poll thread
void spdk_yield_thread() {
	_lthread_yield_cb(lthread_self(), __scheduler_enqueue, lthread_self());
}

static void poll_thread(void *arg)
{
	struct spdk_dev *dev = arg;
	struct spdk_nvme_qpair *qpair;
	// In future we want to process one queue per core instead!
	while (!dev->stop_polling) {
		for (int i = 0; i < dev->ns_entry.qpairs_num; i++) {
			qpair = dev->ns_entry.qpairs[i];
			if (qpair) {
				spdk_nvme_qpair_process_completions(qpair, 0);
			}
		}
		_lthread_yield_cb(lthread_self(), __scheduler_enqueue, lthread_self());
	}
}

int sgxlkl_register_spdk_device(struct spdk_dev *dev)
{
	struct lkl_ifreq ifr;
	int fd, err;

	dev->stop_polling = false;
	dev->poll_tid = lkl_host_ops.thread_create(poll_thread, dev);
	if (dev->poll_tid == 0) {
		fprintf(stderr, "spdk: failed to start spdk poll thread\n");
		return -EAGAIN;
	}

	fd = lkl_sys_open("/dev/spdk-control", LKL_O_RDONLY, 0);

	if (fd < 0) {
		fprintf(stderr, "spdk: cannot open /dev/spdk-control: %s\n", lkl_strerror(-fd));
		return fd;
	}

	err = lkl_sys_ioctl(fd, SPDK_CTL_ADD, (long)&dev->ns_entry);

	lkl_sys_close(fd);
	if (err < 0) {
		fprintf(stderr, "spdk: ioctl SPDK_CTL_ADD failed: %s\n", lkl_strerror(-fd));
	}

	return err;
}

void final_flush_complete(void* ctx)
{
	bool* ready = (bool*)ctx;
	*ready = true;
}

void sgxlkl_unregister_spdk_device(struct spdk_dev *dev)
{
	// drain also i/o queues here!
	bool ready;
	struct spdk_nvme_qpair *qpair;

	for (int i = 0; i < dev->ns_entry.qpairs_num; i++) {
		qpair = dev->ns_entry.qpairs[i];
		if (qpair) {
			ready = false;
			spdk_nvme_ns_cmd_flush(dev->ns_entry.ns, qpair, final_flush_complete, &ready);
			while (!ready) {
				spdk_nvme_qpair_process_completions(qpair, 0);
			}
		}
	}

	dev->stop_polling = true;
	if (dev->poll_tid > 0) {
		lkl_host_ops.thread_join(dev->poll_tid);
	}
}
