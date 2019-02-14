#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>

#include "dpdk.h"

extern char **environ;
static const int DEBUG_DPDK = 1;

int test_socket(const char* path) {
   int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
   if (sock < 0) {
       return sock;
   }

   struct sockaddr_un name;
   name.sun_family = AF_UNIX;
   strcpy(name.sun_path, path);

   /* Send message. */
   if (sendto(sock, "a", sizeof("a"), 0, (struct sockaddr *)&name,
              sizeof(struct sockaddr_un)) < 0) {
       close(sock);
       return 1;
   }
   close(sock);
   sleep(1);
   return 0;
}

const char *DPDK_MP_SOCKET = "/var/run/dpdk/rte/mp_socket";
int spawn_dpdk_helper(int *pipe_fd) {
    // DPDK uses XDG_RUNTIME_DIR for unprivileged processes.
    // Since we want to use the same socket for both root and us,
    // we simply change our XDG_RUNTIME_DIR.
    assert(pipe_fd);
    *pipe_fd = -1;
    int pipefds[2];
    int r = pipe(pipefds);
    if (r < 0) {
        fprintf(stderr, "[dpdk] pipe failed: %s\n", strerror(errno));
        return r;
    }

    r = putenv("XDG_RUNTIME_DIR=/var/run");
    if (r < 0) {
        fprintf(stderr, "[dpdk] putenv failed: %s\n", strerror(errno));
        return r;
    }

    // the idea is to unlink mp_socket and wait for dpdk-setuid-helper to re-bind it.
    r = unlink(DPDK_MP_SOCKET);
    if (r != 0 && errno != ENOENT) {
        fprintf(stderr, "[dpdk] Failed to remove /var/run/dpdk/rte/mp_socket: %s! Please remove it manually\n", strerror(errno));
        return r;
    }

    char* prog = "dpdk-setuid-helper";
    char* argv[] = {prog, NULL /* pipefd */, NULL /* uid */, "1" /* port-num */, NULL};
    char pipe_arg[255];
    snprintf(pipe_arg, sizeof(pipefds), "%d", pipefds[0]);
    argv[1] = pipe_arg;

    char uid_arg[255];
    snprintf(uid_arg, sizeof(uid_arg), "%d", getuid());
    argv[2] = uid_arg;

    pid_t pid;
    r = posix_spawnp(&pid, "dpdk-setuid-helper", NULL, NULL, argv, environ);
    if (r != 0) {
        fprintf(stderr, "[dpdk] failed to spawn dpdk-setuid-helper\n");
        return r;
    }

    while (1) {
        int r = test_socket(DPDK_MP_SOCKET);
        if (r == 0) {
            fprintf(stderr, "[dpdk] /var/run/dpdk/mp_socket is ready\n");
            *pipe_fd = pipefds[1];
            return 0;
        } else if (r < 0) {
            fprintf(stderr, "[dpdk] failed to test socket /var/run/dpdk/mp_socket: %s\n", strerror(-r));
            return r;
        }

        int status = 0;
        pid_t wpid = waitpid(pid, &status, WNOHANG);
        if (wpid == -1) {
            fprintf(stderr, "[dpdk] failed to wait for dpdk-setuild-helper\n");
            return -EPIPE;
        }
        if (wpid != pid) {
            continue;
        }

        if (WIFEXITED(status)) {
            fprintf(stderr, "[dpdk] dpdk-setuild-helper exited with %d\n", WEXITSTATUS(status));
            return -EPIPE;
        }

        if (WIFSIGNALED(status)) {
            fprintf(stderr, "[dpdk] dpdk-setuild-helper was killed by signal: %d\n", WTERMSIG(status));
            return -EPIPE;
        }
        usleep(100);
    }
};

static char *ealargs[5] = {
       "lkl_vif_dpdk",
       "-c 1",
       "-n 1",
       "--proc-type=secondary",
       "--log-level=0",
};

int dpdk_initialize(enclave_config_t* encl, const char *ifparams)
{
    int ret = 0;
    char poolname[RTE_MEMZONE_NAMESIZE];

    assert(encl->num_dpdk_ifaces);

    struct enclave_dpdk_config *iface = &encl->dpdk_ifaces[encl->num_dpdk_ifaces - 1];
    iface->portid = encl->num_dpdk_ifaces - 1;

    if (DEBUG_DPDK)
        ealargs[4] = "--log-level=debug";

    ret = rte_eal_init(sizeof(ealargs) / sizeof(ealargs[0]),
                       ealargs);
    if (ret < 0) {
        fprintf(stderr, "dpdk: failed to initialize eal\n");
        return ret;
    }

    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "tx-", ifparams);
    iface->txpool = rte_mempool_lookup(poolname);
    fprintf(stderr, "lookup %s\n", poolname);
    if (!iface->txpool) {
        fprintf(stderr, "dpdk: failed to lookup tx pool: %s\n", poolname);
        return -ENOENT;
    }
    snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "rx-", ifparams);
    iface->rxpool = rte_mempool_lookup(poolname);
    fprintf(stderr, "lookup %s\n", poolname);
    if (!iface->rxpool) {
        fprintf(stderr, "dpdk: failed to lookup rx pool: %s\n", poolname);
        return -ENOMEM;
    }

    rte_eth_macaddr_get(iface->portid, (struct ether_addr*)iface->mac);
    fprintf(stderr, "Port %d: %02x:%02x:%02x:%02x:%02x:%02x\n", iface->portid,
            iface->mac[0], iface->mac[1], iface->mac[2], iface->mac[3], iface->mac[4], iface->mac[5]);

    return 0;
}
