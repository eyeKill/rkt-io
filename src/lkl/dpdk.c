#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_net.h>
#include <rte_bus_pci.h>
#include <eal_internal_cfg.h>

#include "lkl/virtio.h"
#include "lkl/dpdk.h"
#include "dpdk_internal.h"
#include "enclave_config.h"

#include "hostcalls.h"

#define MAX_PKT_BURST           16
#define DEBUG 1

struct lkl_netdev_dpdk {
	struct lkl_netdev dev;
	int portid;

	/* burst receive context by rump dpdk code */
	struct rte_mbuf *rcv_mbuf[MAX_PKT_BURST];
	int npkts;
	int bufidx;
	int close: 1;

	struct rte_mempool *rxpool, *txpool; /* ring buffer pool */
};

static int __dpdk_net_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	struct lkl_netdev_dpdk *nd_dpdk;
	int i = 0;
	struct rte_mbuf *rm, *first;
	void *r_data;
	size_t read = 0, r_size, copylen = 0, offset = 0;
	struct lkl_virtio_net_hdr_v1 *header = iov[0].iov_base;
	uint16_t mtu;

	nd_dpdk = (struct lkl_netdev_dpdk *) nd;
	memset(header, 0, sizeof(struct lkl_virtio_net_hdr_v1));

	first = nd_dpdk->rcv_mbuf[nd_dpdk->bufidx];

	for (rm = nd_dpdk->rcv_mbuf[nd_dpdk->bufidx]; rm; rm = rm->next) {
		r_data = rte_pktmbuf_mtod(rm, void *);
		r_size = rte_pktmbuf_data_len(rm);

#ifdef DEBUG
		fprintf(stderr, "dpdk-rx: mbuf pktlen=%d orig_len=%lu\n",
			   r_size, iov[i].iov_len);
#endif
		/* mergeable buffer starts data after vnet header at [0] */
#if 0
		if (nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_MRG_RXBUF) &&
		    i == 0)
			offset = sizeof(struct lkl_virtio_net_hdr_v1);
		else if (nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_GUEST_TSO4) &&
			 i == 0)
			i++;
		else
			offset = sizeof(struct lkl_virtio_net_hdr_v1);
#endif
		offset = sizeof(struct lkl_virtio_net_hdr_v1);

		read += r_size;
		while (r_size > 0) {
			if (i >= cnt) {
				fprintf(stderr,
					"dpdk-rx: buffer full. skip it. ");
				fprintf(stderr,
					"(cnt=%d, buf[%d]=%lu, size=%lu)\n",
					i, cnt, iov[i].iov_len, r_size);
				goto end;
			}

			copylen = r_size < (iov[i].iov_len - offset) ? r_size
				: iov[i].iov_len - offset;
			memcpy(iov[i].iov_base + offset, r_data, copylen);

			r_size -= copylen;
			offset = 0;
			i++;
		}
	}

end:
	/* TSO (big_packet mode) */
	header->flags = LKL_VIRTIO_NET_HDR_F_DATA_VALID;
	rte_eth_dev_get_mtu(nd_dpdk->portid, &mtu);

	if (read > (mtu + sizeof(struct ether_hdr)
		    + sizeof(struct lkl_virtio_net_hdr_v1))) {
		struct rte_net_hdr_lens hdr_lens;
		uint32_t ptype;

		ptype = rte_net_get_ptype(first, &hdr_lens, RTE_PTYPE_ALL_MASK);

		//if ((ptype & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP) {
		//	if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV4 &&
		//	    nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_GUEST_TSO4))
		//		header->gso_type = LKL_VIRTIO_NET_HDR_GSO_TCPV4;
		//	/* XXX: Intel X540 doesn't support LRO
		//	 * with tcpv6 packets
		//	 */
		//	if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV6 &&
		//	    nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_GUEST_TSO6))
		//		header->gso_type = LKL_VIRTIO_NET_HDR_GSO_TCPV6;
		//}

		header->gso_size = mtu - hdr_lens.l3_len - hdr_lens.l4_len;
		header->hdr_len = hdr_lens.l2_len + hdr_lens.l3_len
			+ hdr_lens.l4_len;
	}

	read += sizeof(struct lkl_virtio_net_hdr_v1);

#ifdef DEBUG
	fprintf(stderr, "dpdk-rx: len=%d mtu=%d type=%d, size=%d, hdrlen=%d\n",
			read, mtu, header->gso_type,
			header->gso_size, header->hdr_len);
#endif

	return read;
}
static int sgxlkl_dpdk_tx_prep(struct rte_mbuf *rm,
		struct lkl_virtio_net_hdr_v1 *header)
{
	struct rte_net_hdr_lens hdr_lens;
	uint32_t ptype;

#ifdef DEBUG
	fprintf(stderr, "dpdk-tx: gso_type=%d, gso=%d, hdrlen=%d validation=%d\n",
		header->gso_type, header->gso_size, header->hdr_len,
		rte_validate_tx_offload(rm));
#endif

	ptype = rte_net_get_ptype(rm, &hdr_lens, RTE_PTYPE_ALL_MASK);
	rm->l2_len = hdr_lens.l2_len;
	rm->l3_len = hdr_lens.l3_len;
	rm->l4_len = hdr_lens.l4_len; // including tcp opts

	if ((ptype & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP) {
		if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV4)
			rm->ol_flags = PKT_TX_IPV4;
		else if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV6)
			rm->ol_flags = PKT_TX_IPV6;

		rm->ol_flags |= PKT_TX_TCP_CKSUM;
		rm->tso_segsz = header->gso_size;
		/* TSO case */
		if (header->gso_type == LKL_VIRTIO_NET_HDR_GSO_TCPV4)
			rm->ol_flags |= (PKT_TX_TCP_SEG | PKT_TX_IP_CKSUM);
		else if (header->gso_type == LKL_VIRTIO_NET_HDR_GSO_TCPV6)
			rm->ol_flags |= PKT_TX_TCP_SEG;
	}

	return sizeof(struct lkl_virtio_net_hdr_v1);

}

static int sgxlkl_dpdk_tx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	void *pkt;
	struct rte_mbuf *rm;
	struct lkl_netdev_dpdk *nd_dpdk;
	struct lkl_virtio_net_hdr_v1 *header = NULL;
	int i, len, sent = 0;
	void *data = NULL;

	nd_dpdk = (struct lkl_netdev_dpdk *) nd;

	/*
	 * XXX: someone reported that DPDK's mempool with cache is not thread
	 * safe (e.g., http://www.dpdk.io/ml/archives/dev/2014-February/001401.html),
	 * potentially rte_pktmbuf_alloc() is not thread safe here.  so I
	 * tentatively disabled the cache on mempool by assigning
	 * MEMPOOL_CACHE_SZ to 0.
	 */
	rm = rte_pktmbuf_alloc(nd_dpdk->txpool);

	for (i = 0; i < cnt; i++) {
		data = iov[i].iov_base;
		len = (int)iov[i].iov_len;

		if (i == 0) {
			header = data;
			data += sizeof(*header);
			len -= sizeof(*header);
		}

		if (len == 0)
			continue;

		pkt = rte_pktmbuf_append(rm, len);
		if (pkt) {
			/* XXX: I wanna have M_EXT flag !!! */
			memcpy(pkt, data, len);
			sent += len;
		} else {
			fprintf(stderr, "dpdk-tx: failed to append: idx=%d len=%d\n",
				   i, len);
			rte_pktmbuf_free(rm);
			return -1;
		}
#ifdef DEBUG
		fprintf(stderr, "dpdk-tx: pkt[%d]len=%d\n", i, len);
#endif
	}

	/* preparation for TX offloads */
	sent += sgxlkl_dpdk_tx_prep(rm, header);

	/* XXX: should be bulk-trasmitted !! */
	if (rte_eth_tx_prepare(nd_dpdk->portid, 0, &rm, 1) != 1)
		lkl_printf("tx_prep failed\n");

	uint16_t j = rte_eth_tx_burst(nd_dpdk->portid, 0, &rm, 1);
#ifdef DEBUG
		fprintf(stderr, "dpdk-tx: burst pkt[%d]\n", j);
#endif

	rte_pktmbuf_free(rm);
	return sent;
}

static int sgxlkl_dpdk_rx(struct lkl_netdev *nd, struct lkl__iovec *iov, int cnt)
{
	struct lkl_netdev_dpdk *nd_dpdk;
	int read = 0;

	nd_dpdk = (struct lkl_netdev_dpdk *) nd;

	if (nd_dpdk->npkts == 0) {
		nd_dpdk->npkts = rte_eth_rx_burst(nd_dpdk->portid, 0,
						  nd_dpdk->rcv_mbuf,
						  MAX_PKT_BURST);
		if (nd_dpdk->npkts <= 0) {
			/* XXX: need to implement proper poll()
			 * or interrupt mode PMD of dpdk, which is only
			 * available on ixgbe/igb/e1000 (as of Jan. 2016)
			 */
			//if (!nd_dpdk->busy_poll)
			usleep(1);
			return -1;
		}
		nd_dpdk->bufidx = 0;
	}
    fprintf(stderr, "%s() rte_eth_devices: %d pkts\n", __func__, nd_dpdk->npkts);

    /* mergeable buffer */
	read = __dpdk_net_rx(nd, iov, cnt);

	rte_pktmbuf_free(nd_dpdk->rcv_mbuf[nd_dpdk->bufidx]);

	nd_dpdk->bufidx++;
	nd_dpdk->npkts--;

	return read;
}

static int sgxlkl_dpdk_poll(struct lkl_netdev *nd)
{
	struct lkl_netdev_dpdk *nd_dpdk =
		container_of(nd, struct lkl_netdev_dpdk, dev);

	//fprintf(stderr, "%s at %s:%d: %p\n", __func__, __FILE__, __LINE__,
	//		nd_dpdk);

	if (nd_dpdk->close)
		return LKL_DEV_NET_POLL_HUP;
	/*
	 * dpdk's interrupt mode has equivalent of epoll_wait(2),
	 * which we can apply here. but AFAIK the mode is only available
	 * on limited NIC drivers like ixgbe/igb/e1000 (with dpdk v2.2.0),
	 * while vmxnet3 is not supported e.g..
	 */

	usleep(10);
	return LKL_DEV_NET_POLL_RX | LKL_DEV_NET_POLL_TX;
}

static void sgxlkl_dpdk_poll_hup(struct lkl_netdev *nd)
{
	struct lkl_netdev_dpdk *nd_dpdk =
		container_of(nd, struct lkl_netdev_dpdk, dev);

	fprintf(stderr, "%s at %s:%d\n", __func__, __FILE__, __LINE__);
	nd_dpdk->close = 1;
}

static void sgxlkl_dpdk_free(struct lkl_netdev *nd)
{
	struct lkl_netdev_dpdk *nd_dpdk =
		container_of(nd, struct lkl_netdev_dpdk, dev);

	fprintf(stderr, "%s at %s:%d\n", __func__, __FILE__, __LINE__);
	free(nd_dpdk);
}

static struct lkl_dev_net_ops dpdk_net_ops =
{
	 .tx = sgxlkl_dpdk_tx,
	 .rx = sgxlkl_dpdk_rx,
	 .poll = sgxlkl_dpdk_poll,
	 .poll_hup = sgxlkl_dpdk_poll_hup,
	 .free = sgxlkl_dpdk_free,
};

struct lkl_netdev* sgxlkl_register_netdev_dpdk(struct enclave_dpdk_config *dpdk_iface) {
	struct lkl_netdev_dpdk *nd = malloc(sizeof(struct lkl_netdev_dpdk));
	memset(nd, 0, sizeof(struct lkl_netdev_dpdk));
	nd->dev.ops = &dpdk_net_ops;
	nd->portid = dpdk_iface->portid;
    nd->rxpool = dpdk_iface->rxpool;
    nd->txpool = dpdk_iface->txpool;

	return (struct lkl_netdev*)nd;
}

// List of reset function exported from dpdk
int i40evf_dev_init(struct rte_eth_dev *dev);
struct dev_init_function {
    char* driver_name;
    int (*dev_init)(struct rte_eth_dev *eth_dev);
};

static struct dev_init_function dev_init_table[1] = {
   { .driver_name = "net_i40e", .dev_init = i40evf_dev_init  },
};

eth_dev_reset_t find_dev_reset_function(struct rte_eth_dev *device) {
    for (int i = 0; i < sizeof(dev_init_table); i++) {
        const char* name = device->device->driver->name;
        if (strcmp(device->device->driver->name, dev_init_table[i].driver_name) == 0) {
            return dev_init_table[i].dev_init;
        }
    }
    return NULL;
}

void mp_hdlr_init_ops_mp_mc(void);
void mp_hdlr_init_ops_mp_sc(void);
void mp_hdlr_init_ops_sp_mc(void);
void mp_hdlr_init_ops_sp_sc(void);

#warning "Blindly trusting outside structures might make the application vulnerable. Remove this for production"

int sgxlkl_register_dpdk_context(struct dpdk_context *context) {
    memcpy(rte_eth_devices, context->devices, sizeof(struct rte_eth_dev[RTE_MAX_ETHPORTS]));
    memcpy(rte_eal_get_configuration(), context->config, sizeof(struct rte_config));
    memcpy(lcore_config, context->lcore_config, sizeof(struct lcore_config[RTE_MAX_LCORE]));
    memcpy(&internal_config, context->internal_config, sizeof(struct internal_config));

    for (int portid = 0; portid < RTE_MAX_ETHPORTS; portid++) {
       struct rte_mempool *rxpool, *txpool; /* ring buffer pool */

       struct rte_eth_dev *device = &rte_eth_devices[portid];
       if (!device->device) {
           continue;
       }
       eth_dev_reset_t reset_func = find_dev_reset_function(device);
       if (!reset_func) {
           fprintf(stderr, "[    SGX-LKL   ] Failed to find driver function for %s", device->device->driver->name);
           return -ENOENT;
       }
       // we need to reset the drivers to fixup function pointers
       reset_func(device);
    }

    // I could not convince the linke to include those initilizer, hence we reference them here
    mp_hdlr_init_ops_mp_mc();
    mp_hdlr_init_ops_mp_sc();
    mp_hdlr_init_ops_sp_mc();
    mp_hdlr_init_ops_sp_sc();

    return 0;
}
