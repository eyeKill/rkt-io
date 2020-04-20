#include <rte_config.h>

#define DPDK_MAX_PKT_BURST 16
#define DPDK_MEMPOOL_CACHE_SZ 256
/* assuming MTU == 1500 */
#define DPDK_MBUF_NUM (512 * 4) /* vmxnet3 requires 1024 */
#define DPDK_NUMDESC 512 /* nb_min on vmxnet3 is 512 */
// we only can support one queue at the moment
#define DPDK_NUM_TX_QUEUE 1

#define DPDK_MAX_PACKET_SZ (65535 - (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM))
//#define DPDK_MAX_PACKET_SZ (1500 - (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM))
#define DPDK_MBUF_SIZ \
    (DPDK_MAX_PACKET_SZ + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)

