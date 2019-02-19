#include <unistd.h>

#include "enclave_config.h"

int spawn_dpdk_helper(int *pipe_fd);
int dpdk_initialize_iface(enclave_config_t* encl, const char *ifparams);
struct dpdk_context *dpdk_initialize_context();
