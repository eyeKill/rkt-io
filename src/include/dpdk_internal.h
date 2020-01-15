#ifndef _DPDK_INTERNAL_H
#define _DPDK_INTERNAL_H

#include <eal_internal_cfg.h>
#include <rte_config.h>

struct dpdk_context {
    struct rte_eth_dev *devices;
    struct rte_config *config;
    struct lcore_config *lcore_config;
    struct internal_config *internal_config;
};
void dpdk_init_array();

#endif
