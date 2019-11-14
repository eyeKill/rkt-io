#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>

#include "dpdk.h"
#include "dpdk_internal.h"

static struct dpdk_context dpdk_context;

int dpdk_initialize_iface(enclave_config_t* encl, const char *ifparams)
{
    char poolname[RTE_MEMZONE_NAMESIZE];

    if (!encl->num_dpdk_ifaces) {
        fprintf(stderr, "dpdk: no interface found, skip initialization\n");
        return 0;
    }

    struct enclave_dpdk_config *iface = &encl->dpdk_ifaces[encl->num_dpdk_ifaces - 1];
    iface->portid = encl->num_dpdk_ifaces - 1;

    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "tx-", ifparams);
    iface->txpool = rte_mempool_lookup(poolname);
    if (!iface->txpool) {
        fprintf(stderr, "dpdk: failed to lookup tx pool: %s\n", poolname);
        return -ENOENT;
    }
    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "rx-", ifparams);
    iface->rxpool = rte_mempool_lookup(poolname);
    if (!iface->rxpool) {
        fprintf(stderr, "dpdk: failed to lookup rx pool: %s\n", poolname);
        return -ENOMEM;
    }
    return 0;
}

struct dpdk_context *dpdk_initialize_context()
{
    dpdk_context.devices = &rte_eth_devices;
    dpdk_context.config = rte_eal_get_configuration();
    dpdk_context.lcore_config = &lcore_config;
    dpdk_context.internal_config = &internal_config;
    return &dpdk_context;
}
