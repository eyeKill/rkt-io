#ifndef _LKL_LIB_VIRTIO_QUEUE_H
#define _LKL_LIB_VIRTIO_QUEUE_H

#include <stdint.h>
#include <lkl_host.h>
#include <lkl/linux/virtio_ring.h>

#include "lkl/virtio.h"

struct _virtio_req {
	struct virtio_req req;
	struct virtio_dev *dev;
	struct virtio_queue *q;
	uint16_t idx;
};

void async_virtio_process_queue(struct virtio_dev *dev, uint32_t qidx);

#endif
