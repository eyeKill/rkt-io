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
 */
struct lkl_netdev* sgxlkl_register_netdev_dpdk(struct enclave_dpdk_config *dpdk_iface);
int sgxlkl_register_dpdk_context(struct dpdk_context *context);

#endif
