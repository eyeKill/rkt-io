/*
 * Copyright 2019 Imperial College London
 */

#ifndef _MUSLKL_DPDK_H
#define _MUSLKL_DPDK_H

#include <lkl_host.h>

#include "enclave_config.h"

struct ifreq;

/**
 * lkl_register_netdev_dpdk - a dpdk device as a NIC
 *
 * @dev - a POSIX file descriptor number for input/output
 * @returns a struct lkl_netdev_linux_fdnet entry for virtio-net
 */
struct lkl_netdev* sgxlkl_register_netdev_dpdk(struct enclave_dpdk_config *dpdk_iface, char mac[6]);

#endif
