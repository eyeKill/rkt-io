#include "dpdk.h"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_net.h>

#define MAX_PKT_BURST 16
/* XXX: disable cache due to no thread-safe on mempool cache. */
#define MEMPOOL_CACHE_SZ 0
/* for TSO pkt */
#define MAX_PACKET_SZ (65535 - (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM))
#define MBUF_NUM (512 * 2) /* vmxnet3 requires 1024 */
#define MBUF_SIZ \
    (MAX_PACKET_SZ + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NUMDESC 512 /* nb_min on vmxnet3 is 512 */
#define NUMQUEUE 1

int setup_iface(int portid, int mtu) {
    int ret = 0;
    struct rte_eth_conf portconf;
    struct rte_eth_link link;
    struct rte_eth_dev_info dev_info;
    char poolname[RTE_MEMZONE_NAMESIZE];

    char ifparams[6];
    int r = snprintf(ifparams, 6, "dpdk%d", portid);

    memset(&portconf, 0, sizeof(portconf));

    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "tx-", ifparams);
    struct rte_mempool *txpool = rte_mempool_create(
        poolname, MBUF_NUM, MBUF_SIZ, MEMPOOL_CACHE_SZ,
        sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
        rte_pktmbuf_init, NULL, 0, 0);
    if (!txpool) {
        fprintf(stderr, "dpdk: failed to allocate tx pool\n");
        return -ENOMEM;
    }

    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "rx-", ifparams);
    struct rte_mempool *rxpool = rte_mempool_create(
        poolname, MBUF_NUM, MBUF_SIZ, 0,
        sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
        rte_pktmbuf_init, NULL, 0, 0);
    if (!rxpool) {
        fprintf(stderr, "dpdk: failed to allocate rx pool\n");
        return -ENOMEM;
    }

    rte_eth_dev_info_get(portid, &dev_info);
    // if (dev_info.tx_offload_capa & DEV_RX_OFFLOAD_TCP_LRO)
    //    port_conf.txmode.offloads |= DEV_RX_OFFLOAD_TCP_LRO;

    memset(&portconf, 0, sizeof(portconf));
    // portconf.rxmode.offloads |= DEV_RX_OFFLOAD_TCP_LRO;
    // portconf.rxmode.offloads &= ~DEV_RX_OFFLOAD_KEEP_CRC;
    portconf.rxmode.max_rx_pkt_len = ETHER_MAX_LEN;

    ret = rte_eth_dev_configure(portid, NUMQUEUE, NUMQUEUE, &portconf);
    if (ret < 0) {
        fprintf(stderr, "dpdk: failed to configure port\n");
        return ret;
    }

    ret = rte_eth_dev_set_mtu(portid, mtu);
    if (ret < 0) {
        fprintf(stderr, "dpdk: failed to set mtu\n");
        return ret;
    }

    ret = rte_eth_rx_queue_setup(portid, 0, NUMDESC, 0,
                                 &dev_info.default_rxconf, rxpool);
    if (ret < 0) {
        fprintf(stderr, "dpdk: failed to setup rx queue\n");
        return ret;
    }

    dev_info.default_txconf.offloads = 0;

    // TODO check capabilities before applying this
    // dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_MULTI_SEGS;
    // dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_UDP_CKSUM;
    // dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_TCP_CKSUM;

    ret =
        rte_eth_tx_queue_setup(portid, 0, NUMDESC, 0, &dev_info.default_txconf);
    if (ret < 0) {
        fprintf(stderr, "dpdk: failed to setup tx queue\n");
        return ret;
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
