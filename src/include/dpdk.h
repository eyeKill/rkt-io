#include <unistd.h>

#include "enclave_config.h"

int spawn_dpdk_helper(int *pipe_fd);
int dpdk_initialize(enclave_config_t* encl, const char *ifparams);
