/*
 * Copyright 2019 Imperial College London
 */

#ifndef _MUSLKL_DPDK_H
#define _MUSLKL_DPDK_H

#include "sgx_enclave_config.h"

/**
 * lkl_register_netdev_dpdk - a dpdk device as a NIC
 */

int sgxlkl_register_dpdk_device(struct enclave_dpdk_config *dev);
int sgxlkl_register_dpdk_context(struct dpdk_context *context);

#endif
