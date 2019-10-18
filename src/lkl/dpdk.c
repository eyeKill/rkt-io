#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>

#include <eal_internal_cfg.h>
#include <rte_bus_pci.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_net.h>
#include <linux/dpdk.h>

#include "dpdk_internal.h"
#include "lkl/dpdk.h"
#include "lkl/virtio.h"
#include "lthread_int.h"
#include "pcap.h"
#include "sgx_enclave_config.h"

#include "sgx_hostcalls.h"

int sgxlkl_register_dpdk_device(struct enclave_dpdk_config *config) {
    struct lkl_ifreq ifr;
    int fd, err;
    struct dpdk_dev dev = {};
    dev.portid = config->portid;
    dev.rxpool = config->rxpool;
    dev.txpool = config->txpool;

    memcpy(&dev.mac, &config->mac, sizeof(dev.mac));

    fd = lkl_sys_open("/dev/dpdk-control", LKL_O_RDONLY, 0);

    if (fd < 0) {
        fprintf(stderr, "dpdk: cannot open /dev/dpdk-control: %s\n",
                lkl_strerror(-fd));
        return fd;
    }


    err = lkl_sys_ioctl(fd, DPDK_CTL_ADD, (long)&dev);

    lkl_sys_close(fd);

    if (err < 0) {
        fprintf(stderr, "dpdk: ioctl DPDK_CTL_ADD failed: %s\n",
                lkl_strerror(-fd));
    }

    return err;
}

// List of reset function exported from dpdk
int i40evf_dev_init(struct rte_eth_dev *dev);
struct dev_init_function {
    char *driver_name;
    int (*dev_init)(struct rte_eth_dev *eth_dev);
};

static struct dev_init_function dev_init_table[1] = {
    {.driver_name = "net_i40e", .dev_init = i40evf_dev_init},
};

eth_dev_reset_t find_dev_reset_function(struct rte_eth_dev *device) {
    for (int i = 0; i < sizeof(dev_init_table); i++) {
        const char *name = device->device->driver->name;
        if (strcmp(device->device->driver->name,
                   dev_init_table[i].driver_name) == 0) {
            return dev_init_table[i].dev_init;
        }
    }
    return NULL;
}

#warning \
    "Blindly trusting outside structures might make the application vulnerable. Remove this for production"

int sgxlkl_register_dpdk_context(struct dpdk_context *context) {
    // since we are compiling without compiling without stdlib, dpdk's library
    // initializer functions are not called
    memcpy(rte_eth_devices, context->devices,
           sizeof(struct rte_eth_dev[RTE_MAX_ETHPORTS]));
    memcpy(rte_eal_get_configuration(), context->config,
           sizeof(struct rte_config));
    memcpy(lcore_config, context->lcore_config,
           sizeof(struct lcore_config[RTE_MAX_LCORE]));
    memcpy(&internal_config, context->internal_config,
           sizeof(struct internal_config));

    dpdk_init_array();

    for (int portid = 0; portid < RTE_MAX_ETHPORTS; portid++) {
        struct rte_mempool *rxpool, *txpool; /* ring buffer pool */

        struct rte_eth_dev *device = &rte_eth_devices[portid];
        if (!device->device) {
            continue;
        }
        eth_dev_reset_t reset_func = find_dev_reset_function(device);
        if (!reset_func) {
            fprintf(stderr,
                    "[    SGX-LKL   ] Failed to find driver function for %s",
                    device->device->driver->name);
            return -ENOENT;
        }
        // we need to reset the drivers to fixup function pointers
        reset_func(device);
    }

    return 0;
}
