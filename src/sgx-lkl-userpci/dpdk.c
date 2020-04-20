#include "dpdk.h"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_net.h>
#include <dpdk_config.h>

static struct rte_eth_conf port_conf = {
// TODO: more optimizations:
// if (dev_info.tx_offload_capa & DEV_RX_OFFLOAD_TCP_LRO)
//    port_conf.txmode.offloads |= DEV_RX_OFFLOAD_TCP_LRO;
// portconf.rxmode.offloads |= DEV_RX_OFFLOAD_TCP_LRO;
// portconf.rxmode.offloads &= ~DEV_RX_OFFLOAD_KEEP_CRC;
// TODO check capabilities before applying this
// dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_MULTI_SEGS;
// dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_UDP_CKSUM;
// dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_TCP_CKSUM;
  .rxmode = {
    .mq_mode = ETH_MQ_RX_RSS,
    .offloads = DEV_RX_OFFLOAD_CHECKSUM
      | DEV_TX_OFFLOAD_TCP_TSO
      | DEV_TX_OFFLOAD_UDP_TSO
      | DEV_RX_OFFLOAD_SCATTER,
    .max_rx_pkt_len = ETHER_MAX_LEN,
  },
  .rx_adv_conf = {
    .rss_conf = {
        // taken from i40e_ethdev.h, other hardware might support different offloads
        .rss_hf = ETH_RSS_NONFRAG_IPV4_TCP \
          | ETH_RSS_NONFRAG_IPV4_UDP \
          | ETH_RSS_NONFRAG_IPV4_SCTP \
          | ETH_RSS_NONFRAG_IPV6_TCP \
          | ETH_RSS_NONFRAG_IPV6_UDP \
          | ETH_RSS_NONFRAG_IPV6_SCTP,
    }
   },
};


static int sym_hash_enable(int port_id, uint32_t ftype,
		    enum rte_eth_hash_function function)
{
	struct rte_eth_hash_filter_info info;
	int ret = 0;
	uint32_t idx = 0;
	uint32_t offset = 0;

	memset(&info, 0, sizeof(info));

	ret = rte_eth_dev_filter_supported(port_id, RTE_ETH_FILTER_HASH);
	if (ret < 0) {
		fprintf(stderr, "dpdk: RTE_ETH_FILTER_HASH not supported on port: %d\n", port_id);
		return ret;
	}

	info.info_type = RTE_ETH_HASH_FILTER_GLOBAL_CONFIG;
	info.info.global_conf.hash_func = function;

	idx = ftype / UINT64_BIT;
	offset = ftype % UINT64_BIT;
	info.info.global_conf.valid_bit_mask[idx] |= (1ULL << offset);
	info.info.global_conf.sym_hash_enable_mask[idx] |= (1ULL << offset);

	ret = rte_eth_dev_filter_ctrl(port_id, RTE_ETH_FILTER_HASH,
				      RTE_ETH_FILTER_SET, &info);
	if (ret < 0) {
		fprintf(stderr, "dpdk: Cannot set global hash configurations on port %u\n", port_id);
		return ret;
	}

	return 0;
}

int sym_hash_set(int port_id, int enable)
{
	int ret = 0;
	struct rte_eth_hash_filter_info info;

	memset(&info, 0, sizeof(info));

	ret = rte_eth_dev_filter_supported(port_id, RTE_ETH_FILTER_HASH);
	if (ret < 0) {
		fprintf(stderr, "dpdk: RTE_ETH_FILTER_HASH not supported on port: %d\n", port_id);
		return ret;
	}

	info.info_type = RTE_ETH_HASH_FILTER_SYM_HASH_ENA_PER_PORT;
	info.info.enable = enable;
	ret = rte_eth_dev_filter_ctrl(port_id, RTE_ETH_FILTER_HASH,
				      RTE_ETH_FILTER_SET, &info);

	if (ret < 0) {
		fprintf(stderr, "dpdk: Cannot set symmetric hash enable per port on port %u\n", port_id);
		return ret;
	}

	return 0;
}

int enable_symmetric_rxhash(int port_id) {
  int r;
  r |= sym_hash_enable(port_id, RTE_ETH_FLOW_NONFRAG_IPV4_TCP,
                  RTE_ETH_HASH_FUNCTION_TOEPLITZ);
  r |= sym_hash_enable(port_id, RTE_ETH_FLOW_NONFRAG_IPV4_UDP,
                       RTE_ETH_HASH_FUNCTION_TOEPLITZ);
  r |= sym_hash_enable(port_id, RTE_ETH_FLOW_FRAG_IPV4,
                  RTE_ETH_HASH_FUNCTION_TOEPLITZ);
  r |= sym_hash_enable(port_id, RTE_ETH_FLOW_NONFRAG_IPV4_SCTP,
                  RTE_ETH_HASH_FUNCTION_TOEPLITZ);
  r |= sym_hash_enable(port_id, RTE_ETH_FLOW_NONFRAG_IPV4_OTHER,
                  RTE_ETH_HASH_FUNCTION_TOEPLITZ);

  r |= sym_hash_set(port_id, 1);
  return r;
}

int setup_iface(int portid, size_t mtu, size_t rx_queues) {
    int ret = 0;
    struct rte_eth_link link;
    struct rte_eth_dev_info dev_info;
    char poolname[RTE_MEMZONE_NAMESIZE];

    char ifparams[6];
    int r = snprintf(ifparams, 6, "dpdk%d", portid);

    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "tx-%s", ifparams);
    struct rte_mempool *txpool = rte_mempool_create(
        poolname, DPDK_MBUF_NUM, DPDK_MBUF_SIZ, DPDK_MEMPOOL_CACHE_SZ,
        sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
        rte_pktmbuf_init, NULL, 0, 0);
    if (!txpool) {
        fprintf(stderr, "dpdk: failed to allocate tx pool\n");
        return -ENOMEM;
    }

    rte_eth_dev_info_get(portid, &dev_info);

    if ((port_conf.rx_adv_conf.rss_conf.rss_hf & dev_info.flow_type_rss_offloads) !=
        port_conf.rx_adv_conf.rss_conf.rss_hf) {
      fprintf(stderr, "dpdk: not all rss offloads requested supported by this hardware. You might need to adapt %s\n", __FILE__);
      return -ENOSYS;
    }

    if ((port_conf.txmode.offloads & dev_info.tx_offload_capa) !=
        port_conf.txmode.offloads) {
      fprintf(stderr, "dpdk: not all tx offloads requested supported by this hardware. You might need to adapt %s\n", __FILE__);
      return -ENOSYS;
    }

    ret = rte_eth_dev_configure(portid, rx_queues, DPDK_NUM_TX_QUEUE, &port_conf);
    if (ret < 0) {
      fprintf(stderr, "dpdk: failed to configure port: %s\n", rte_strerror(-ret));
      return ret;
    }
    // https://haryachyy.wordpress.com/2019/01/18/learning-dpdk-symmetric-rss/
    ret = enable_symmetric_rxhash(portid);
    if (ret < 0) {
      fprintf(stderr, "dpdk: failed to set rxhash: %s\n", rte_strerror(-ret));
      return ret;
    }

    ret = rte_eth_dev_set_mtu(portid, mtu);
    if (ret < 0) {
      fprintf(stderr, "dpdk: failed to set mtu: %s\n", rte_strerror(-ret));
      return ret;
    }

    dev_info.default_rxconf.offloads = 0;
    dev_info.default_txconf.offloads = 0;

    for (unsigned i = 0; i < rx_queues; i++) {
      snprintf(poolname, RTE_MEMZONE_NAMESIZE, "rx-%u-%s", i, ifparams);
      struct rte_mempool *rxpool = rte_mempool_create(poolname,
                                                       DPDK_MBUF_NUM,
                                                       DPDK_MBUF_SIZ,
                                                       DPDK_MEMPOOL_CACHE_SZ,
                                                       sizeof(struct rte_pktmbuf_pool_private),
                                                       rte_pktmbuf_pool_init, NULL,
                                                       rte_pktmbuf_init, NULL, 0, 0);
      if (!rxpool) {
        fprintf(stderr, "dpdk: failed to allocate rx pool %s\n", poolname);
        return -ENOMEM;
      }
      ret = rte_eth_rx_queue_setup(portid, i, DPDK_NUMDESC, 0, &dev_info.default_rxconf, rxpool);
      if (ret < 0) {
        fprintf(stderr, "dpdk: failed to setup rx queue %u: %s\n", i, rte_strerror(-ret));
        return ret;
      }
    }

    for (unsigned i = 0; i < DPDK_NUM_TX_QUEUE; i++) {
      ret = rte_eth_tx_queue_setup(portid, i, DPDK_NUMDESC, 0, &dev_info.default_txconf);
      if (ret < 0) {
        fprintf(stderr, "dpdk: failed to setup tx queue %u: %s\n", i, rte_strerror(-ret));
        return ret;
      }
    }

    ret = rte_eth_dev_start(portid);
    /* XXX: this function returns positive val (e.g., 12)
     * if there's an error
     */
    if (ret != 0) {
        fprintf(stderr, "dpdk: failed to setup tx queue\n");
        return ret;
    }

    rte_eth_dev_set_link_up(portid);

    rte_eth_link_get(portid, &link);
    if (!link.link_status) {
        fprintf(stderr, "dpdk: interface state is down\n");
        rte_eth_link_get(portid, &link);
        if (!link.link_status) {
            fprintf(stderr, "dpdk: interface state is down.. Giving up.\n");
            return -EIO;
        }
        fprintf(stderr, "dpdk: interface state should be up now.\n");
    }

    /* should be promisc ? */
    rte_eth_promiscuous_enable(portid);

    return 0;
}
