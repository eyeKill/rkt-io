#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dpdk.h"

static const int DEBUG_DPDK = 1;

static char *ealargs[5] = {
    "lkl_vif_dpdk", "-c 1", "-n 1", "--log-level=0", "--proc-type=primary",
};

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "USAGE: %s pipe-fd uid port-num\n", argv[0]);
        return 1;
    }
    int pipe_fd = atoi(argv[1]);
    int uid = atoi(argv[2]);
    int port_num = atoi(argv[3]);

    if (DEBUG_DPDK)
        ealargs[3] = "--log-level=debug";

    int ret = rte_eal_init(sizeof(ealargs) / sizeof(ealargs[0]),
                       ealargs);
	if (ret < 0) {
        fprintf(stderr, "dpdk: failed to initialize eal: %s\n", strerror(ret));
        return 1;
    }

    for (int portid = 0; portid < port_num; portid++) {
        int r = setup_iface(portid);
        if (r < 0) {
            return 1;
        }
    }

    char cmd_tmpl[] = "spdk-fix-permissions.sh %d";
    size_t needed = snprintf(NULL, 0, cmd_tmpl, uid) + 1;
    char *cmd = malloc(needed);
    if (!cmd) {
        fprintf(stderr, "dpdk: out of memory");
        return 1;
    }
    snprintf(cmd, needed, cmd_tmpl, uid) + 1;

    #warning "Rewrite this in C for production code!. Call bash scripts from setuid is insecure for a reason"
    setuid(0);
    int r = system(cmd);
    free(cmd);

    if (r != 0) {
        fprintf(stderr, "dpdk: failed to chown dpdk files: %d\n", r);
        return 1;
    }

    // child will eventually close this
    char byte;
    read(pipe_fd, &byte, 1);
    fprintf(stderr, "dpdk: stop setuid-helper\n");
}
