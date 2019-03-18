#define _BSD_SOURCE
#include "lkl/virtio_queue.h"

#include <stdio.h>
#include <endian.h>
#include <stdint.h>
#include "lkl/virtio.h"
#include <lkl/linux/virtio_ring.h>

struct virtio_queue {
	uint32_t num_max;
	uint32_t num;
	uint32_t ready;
	uint32_t max_merge_len;

	struct lkl_vring_desc *desc;
	struct lkl_vring_avail *avail;
	struct lkl_vring_used *used;
	uint16_t last_avail_idx;
    uint16_t last_queued_idx;
	uint16_t last_used_idx_signaled;
};


#define virtio_panic(msg, ...) do {                         \
		lkl_printf("LKL virtio error" msg, ##__VA_ARGS__);	\
		lkl_host_ops.panic();					\
	} while (0)

/*
 * Grab the vring_desc from the queue at the appropriate index in the
 * queue's circular buffer, converting from little-endian to
 * the host's endianness.
 */
static inline
struct lkl_vring_desc *vring_desc_at_le_idx(struct virtio_queue *q, uint16_t le_idx)
{
	return &q->desc[le16toh(le_idx) & (q->num -1)];
}

static inline
struct lkl_vring_desc *vring_desc_at_avail_idx(struct virtio_queue *q, uint16_t idx)
{
	uint16_t desc_idx = q->avail->ring[idx & (q->num - 1)];

	return vring_desc_at_le_idx(q, desc_idx);
}

static inline void virtio_set_avail_event(struct virtio_queue *q, uint16_t val)
{
	*((uint16_t *)&q->used->ring[q->num]) = val;
}

static struct lkl_vring_desc *get_next_desc(struct virtio_queue *q,
					    struct lkl_vring_desc *desc,
					    uint16_t *idx)
{
	uint16_t desc_idx;

	if (q->max_merge_len) {
		if (++(*idx) == le16toh(q->avail->idx))
			return NULL;
		desc_idx = q->avail->ring[*idx & (q->num - 1)];
		return vring_desc_at_le_idx(q, desc_idx);
	}

	if (!(le16toh(desc->flags) & LKL_VRING_DESC_F_NEXT))
		return NULL;
	return vring_desc_at_le_idx(q, desc->next);
}

/* Initialize buf to hold the same info as the vring_desc */
static void add_dev_buf_from_vring_desc(struct virtio_req *req,
					struct lkl_vring_desc *vring_desc)
{
	struct iovec *buf = &req->buf[req->buf_count++];

	buf->iov_base = (void *)(uintptr_t)le64toh(vring_desc->addr);
	buf->iov_len = le32toh(vring_desc->len);

	if (!(buf->iov_base && buf->iov_len))
		virtio_panic("bad vring_desc: %p %d\n",
			     buf->iov_base, buf->iov_len);

	req->total_len += buf->iov_len;
}

static int virtio_process_one(struct virtio_dev *dev, int qidx)
{
	struct virtio_queue *q = &dev->queue[qidx];
    uint16_t idx = q->last_queued_idx;
    struct _virtio_req _req = {
		.dev = dev,
		.q = q,
		.idx = idx,
	};
	struct virtio_req *req = &_req.req;
	struct lkl_vring_desc *desc = vring_desc_at_avail_idx(q, _req.idx);

	do {
		add_dev_buf_from_vring_desc(req, desc);
		if (q->max_merge_len && req->total_len > q->max_merge_len)
			break;
		desc = get_next_desc(q, desc, &idx);
	} while (desc && req->buf_count < VIRTIO_REQ_MAX_BUFS);

	if (desc && le16toh(desc->flags) & LKL_VRING_DESC_F_NEXT)
		virtio_panic("too many chained bufs");

	return dev->ops->enqueue(dev, qidx, req);
}

void async_virtio_process_queue(struct virtio_dev *dev, uint32_t qidx)
{
	struct virtio_queue *q = &dev->queue[qidx];

	if (!q->ready)
		return;

	if (dev->ops->acquire_queue)
		dev->ops->acquire_queue(dev, qidx);

	while (q->last_queued_idx != le16toh(q->avail->idx)) {
		/*
		 * Make sure following loads happens after loading
		 * q->avail->idx.
		 */
		__sync_synchronize();
        int buf_count = virtio_process_one(dev, qidx);
		if (buf_count < 0)
			break;
        q->last_queued_idx += buf_count;
	}

    if (dev->ops->release_queue)
		dev->ops->release_queue(dev, qidx);
}
