#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_net.h>

static char *ealargs[5] = {
    "lkl_vif_dpdk", "-c 1", "-n 1", "--log-level=0", "--proc-type=primary",
};

#define MAX_PKT_BURST           16
/* XXX: disable cache due to no thread-safe on mempool cache. */
#define MEMPOOL_CACHE_SZ        0
/* for TSO pkt */
#define MAX_PACKET_SZ           (65535 \
                                 - (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM))
#define MBUF_NUM                (512*2) /* vmxnet3 requires 1024 */
#define MBUF_SIZ        \
    (MAX_PACKET_SZ + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NUMDESC         512 /* nb_min on vmxnet3 is 512 */
#define NUMQUEUE        1

static const int DEBUG_DPDK = 1;

int setup_iface(int portid) {
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
    struct rte_mempool *rxpool = rte_mempool_create(poolname, MBUF_NUM, MBUF_SIZ, 0,
                                                    sizeof(struct rte_pktmbuf_pool_private),
                                                    rte_pktmbuf_pool_init, NULL,
                                                    rte_pktmbuf_init, NULL, 0, 0);
    if (!rxpool) {
        fprintf(stderr, "dpdk: failed to allocate rx pool\n");
        return -ENOMEM;
    }

    rte_eth_dev_info_get(portid, &dev_info);
    //if (dev_info.tx_offload_capa & DEV_RX_OFFLOAD_TCP_LRO)
    //    port_conf.txmode.offloads |= DEV_RX_OFFLOAD_TCP_LRO;

    memset(&portconf, 0, sizeof(portconf));
    //portconf.rxmode.offloads |= DEV_RX_OFFLOAD_TCP_LRO;
    //portconf.rxmode.offloads &= ~DEV_RX_OFFLOAD_KEEP_CRC;
    portconf.rxmode.max_rx_pkt_len = ETHER_MAX_LEN,

    ret = rte_eth_dev_configure(portid, NUMQUEUE, NUMQUEUE, &portconf);
    if (ret < 0) {
      fprintf(stderr, "dpdk: failed to configure port\n");
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
    //dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_MULTI_SEGS;
    //dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_UDP_CKSUM;
    //dev_info.default_txconf.offloads |= DEV_TX_OFFLOAD_TCP_CKSUM;

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

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "USAGE: %s pipe-fd uid port-num\n", argv[0]);
        return 1;
    }
    int pipe_fd = atoi(argv[1]);
    int uid = atoi(argv[2]);
    int port_num = atoi(argv[3]);

    if (DEBUG_DPDK)
        ealargs[3] = "--log-level=debug";


    int ret = rte_eal_init(sizeof(ealargs) / sizeof(ealargs[0]),
                       ealargs);
	if (ret < 0) {
        fprintf(stderr, "dpdk: failed to initialize eal: %s\n", strerror(ret));
        return 1;
    }

    for (int portid = 0; portid < port_num; portid++) {
        int r = setup_iface(portid);
        if (r < 0) {
            return 1;
        }
    }

    char cmd_tmpl[] = "dpdk-fix-permissions.sh %d";
    size_t needed = snprintf(NULL, 0, cmd_tmpl, uid) + 1;
    char *cmd = malloc(needed);
    if (!cmd) {
        fprintf(stderr, "dpdk: out of memory");
        return 1;
    }
    snprintf(cmd, needed, cmd_tmpl, uid) + 1;

    #warning "Rewrite this in C for production code!. Call bash scripts from setuid is insecure for a reason"
    setuid(0);
    int r = system(cmd);
    free(cmd);

    if (r != 0) {
        fprintf(stderr, "dpdk: failed to chown dpdk files: %d\n", r);
        return 1;
    }

    // child will eventually close this
    char byte;
    read(pipe_fd, &byte, 1);
    fprintf(stderr, "dpdk: stop setuid-helper\n");
}
