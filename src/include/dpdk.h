#include <unistd.h>

#include "enclave_config.h"

int dpdk_initialize_iface(enclave_config_t* encl, const char *ifparams);
struct dpdk_context *dpdk_initialize_context();
