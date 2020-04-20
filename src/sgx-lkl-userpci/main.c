#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rte_ethdev.h>
#include <rte_log.h>

#include "dpdk.h"
#include "spdk_context.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "USAGE: %s ready-fd finished-fd uid\n", argv[0]);
        return 1;
    }
    int ready_fd = atoi(argv[1]);
    int finished_fd = atoi(argv[2]);
    int uid = atoi(argv[3]);
    int exitcode = 0;
    char *mtustr = getenv("SGXLKL_DPDK_MTU");
    char *queues_str = getenv("SGXLKL_DPDK_RX_QUEUES");
    int mtu = 1500;
    size_t rx_queues = 1;

    if (mtustr) {
        mtu = atoi(mtustr);
    }
    if (queues_str) {
        rx_queues = atoi(queues_str);
    }

    // create files with world-writeable permissions (i.e. in /dev/hugepages)
    umask(0);

    // use stderr for logging
    rte_openlog_stream(stderr);

    struct spdk_context ctx = {};
    if (spdk_initialize(&ctx, true) < 0) {
        goto error;
    };

    size_t port_num = rte_eth_dev_count_avail();
    for (int portid = 0; portid < port_num; portid++) {
        int r = setup_iface(portid, mtu, rx_queues);
        if (r < 0) {
            goto error;
        }
    }

    char cmd_tmpl[] = "spdk-fix-permissions.sh %d";
    size_t needed = snprintf(NULL, 0, cmd_tmpl, uid) + 1;
    char *cmd = malloc(needed);
    if (!cmd) {
        fprintf(stderr, "%s: out of memory", argv[0]);
        goto error;
    }
    snprintf(cmd, needed, cmd_tmpl, uid);

#warning \
    "Rewrite this in C for production code!. Call bash scripts from setuid is insecure for a reason"
    int r = setuid(0);
    if (r != 0) {
        fprintf(stderr, "%s: failed to setuid: %d\n", argv[0], r);
        goto error;
    }
    r = system(cmd);
    free(cmd);

    if (r != 0) {
        fprintf(stderr, "%s: failed to chown dpdk files: %d\n", argv[0], r);
        goto error;
    }

    const static char* ready_msg = "OK";
    r = write(ready_fd, ready_msg, strlen(ready_msg) + 1);
    if (r < 0) {
      fprintf(stderr, "%s: failed to write to ready pipe: %s\n", argv[0], strerror(r));
      goto error;
    }
    close(ready_fd);

    // child will eventually close this
    char byte;
    r = read(finished_fd, &byte, 1);
    if (r >= 0) {
      fprintf(stderr, "%s: Got unexpected data result on finished pipe: %d\n", argv[0], r);
      goto error;
    }
    goto cleanup;

error:
    close(ready_fd);
    exitcode = 1;
cleanup:
    spdk_context_detach(&ctx);
    spdk_context_free(&ctx);
    close(finished_fd);
    fprintf(stderr, "%s: stop setuid-helper\n", argv[0]);
    return exitcode;
}
