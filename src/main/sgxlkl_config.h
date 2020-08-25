#ifndef SGXLKL_CONFIG_H
#define SGXLKL_CONFIG_H

#define SGXLKL_APP_CONFIG               0
#define SGXLKL_CMDLINE                  1
#define SGXLKL_CWD                      2
#define SGXLKL_DEBUGMOUNT               3
#define SGXLKL_DPDK_IP4                 4
#define SGXLKL_DPDK_GW4                 5
#define SGXLKL_DPDK_MASK4               6
#define SGXLKL_DPDK_IP6                 7
#define SGXLKL_DPDK_GW6                 8
#define SGXLKL_DPDK_MASK6               9
#define SGXLKL_DPDK_MTU                 10
#define SGXLKL_ENABLE_SGXIO             11
#define SGXLKL_ESPINS                   12
#define SGXLKL_ESLEEP                   13
#define SGXLKL_ETHREADS                 14
#define SGXLKL_ETHREADS_AFFINITY        15
#define SGXLKL_EXIT_ON_HOST_CALLS       16
#define SGXLKL_GETTIME_VDSO             17
#define SGXLKL_GW4                      18
#define SGXLKL_HD                       19
#define SGXLKL_HD_KEY                   20
#define SGXLKL_HD_RO                    21
#define SGXLKL_HDS                      22
#define SGXLKL_HD_VERITY                23
#define SGXLKL_HD_VERITY_OFFSET         24
#define SGXLKL_HEAP                     25
#define SGXLKL_HOSTNAME                 26
#define SGXLKL_HOSTNET                  27
#define SGXLKL_IAS_CERT                 28
#define SGXLKL_IAS_KEY_FILE             29
#define SGXLKL_IAS_QUOTE_TYPE           30
#define SGXLKL_IAS_SERVER               31
#define SGXLKL_IAS_SPID                 32
#define SGXLKL_IP4                      33
#define SGXLKL_KERNEL_VERBOSE           34
#define SGXLKL_KEY                      35
#define SGXLKL_MASK4                    36
#define SGXLKL_MAX_USER_THREADS         37
#define SGXLKL_MMAP_FILES               38
#define SGXLKL_NON_PIE                  39
#define SGXLKL_PRINT_APP_RUNTIME        40
#define SGXLKL_PRINT_HOST_SYSCALL_STATS 41
#define SGXLKL_REAL_TIME_PRIO           42
#define SGXLKL_REMOTE_ATTEST_PORT       43
#define SGXLKL_REMOTE_CMD_PORT          44
#define SGXLKL_REMOTE_CMD_ETH0          45
#define SGXLKL_REMOTE_CONFIG            46
#define SGXLKL_REPORT_NONCE             47
#define SGXLKL_SHMEM_FILE               48
#define SGXLKL_SHMEM_SIZE               49
#define SGXLKL_SIGPIPE                  50
#define SGXLKL_SSLEEP                   51
#define SGXLKL_SSPINS                   52
#define SGXLKL_SPDK_HD_KEY              53
#define SGXLKL_SPDK_SKIP_UNMOUNT        54
#define SGXLKL_STACK_SIZE               55
#define SGXLKL_STHREADS                 56
#define SGXLKL_STHREADS_AFFINITY        57
#define SGXLKL_SYSCTL                   58
#define SGXLKL_TAP                      59
#define SGXLKL_TAP_MTU                  60
#define SGXLKL_TAP_OFFLOAD              61
#define SGXLKL_TRACE_HOST_SYSCALL       62
#define SGXLKL_TRACE_INTERNAL_SYSCALL   63
#define SGXLKL_TRACE_LKL_SYSCALL        64
#define SGXLKL_TRACE_MMAP               65
#define SGXLKL_TRACE_SYSCALL            66
#define SGXLKL_TRACE_THREAD             67
#define SGXLKL_VERBOSE                  68
#define SGXLKL_WAIT_ON_HOST_CALLS       69
#define SGXLKL_WAIT_ON_IO_HOST_CALLS    70
#define SGXLKL_WG_IP                    71
#define SGXLKL_WG_PORT                  72
#define SGXLKL_WG_KEY                   73
#define SGXLKL_WG_PEERS                 74
#define SGXLKL_X86_ACC                  75


#define DEFAULT_SGXLKL_GW4 "10.0.1.254"
/* The default heap size will only be used if no heap size is specified and
 * either we are in simulation mode, or we are in HW mode and a key is provided
 * via SGXLKL_KEY.
 */
#define DEFAULT_SGXLKL_HEAP_SIZE 200 * 1024 * 1024
#define DEFAULT_SGXLKL_HOSTNAME "lkl"
#define DEFAULT_SGXLKL_IAS_QUOTE_TYPE "Unlinkable"
#define DEFAULT_SGXLKL_IAS_SERVER "test-as.sgx.trustedservices.intel.com:443"
#define DEFAULT_SGXLKL_IP4 "10.0.1.1"
#define DEFAULT_SGXLKL_MASK4 24
#define DEFAULT_SGXLKL_MAX_USER_THREADS 256
#define DEFAULT_SGXLKL_ESLEEP 16000
#define DEFAULT_SGXLKL_ETHREADS 1
#define DEFAULT_SGXLKL_STHREADS 4
#define DEFAULT_SGXLKL_ESPINS 500
#define DEFAULT_SGXLKL_SSLEEP 4000
#define DEFAULT_SGXLKL_SSPINS 100
#define DEFAULT_SGXLKL_STACK_SIZE 512 * 1024
#define DEFAULT_SGXLKL_TAP "sgxlkl_tap0"
#define DEFAULT_SGXLKL_REMOTE_ATTEST_PORT 56000
#define DEFAULT_SGXLKL_REMOTE_CMD_PORT 56001
#define DEFAULT_SGXLKL_WG_IP "10.0.42.1"
#define DEFAULT_SGXLKL_WG_PORT 56002
#define DEFAULT_SGXLKL_IP6 "fd16:012a:4639:184d::1"
#define DEFAULT_SGXLKL_GW6 "fe80::1"
#define DEFAULT_SGXLKL_MASK6 64
#define DEFAULT_SGXLKL_DPDK_IP4 "10.0.42.1"
#define DEFAULT_SGXLKL_DPDK_GW4 "10.0.42.254"
#define DEFAULT_SGXLKL_DPDK_MASK4 24
#define DEFAULT_SGXLKL_DPDK_IP6 "fdbf:9188:5fbd:a895::1"
#define DEFAULT_SGXLKL_DPDK_GW6 "fe80::1"
#define DEFAULT_SGXLKL_DPDK_MASK6 64
#define DEFAULT_SGXLKL_DPDK_MTU 1500
#define DEFAULT_SGXLKL_CWD "/"

#define MAX_SGXLKL_ETHREADS 1024
#define MAX_SGXLKL_MAX_USER_THREADS 65536
#define MAX_SGXLKL_STHREADS 1024

int parse_sgxlkl_config(char *path, char **err);
int parse_sgxlkl_config_from_str(char *str, char **err);
int sgxlkl_configured(int opt);
int sgxlkl_config_bool(int opt_key);
uint64_t sgxlkl_config_uint64(int opt_key);
char *sgxlkl_config_str(int opt_key);

#endif /* SGXLKL_CONFIG_H */
