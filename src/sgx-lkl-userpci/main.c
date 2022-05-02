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
    fprintf(stderr, "Starting sgx-lkl-userpci\n");
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
        fprintf(stderr, "userpci: failed to initialize spdk\n");
        goto error;
    };
    fprintf(stderr, "userpci: SPDK initialized.\n");

    size_t port_num = rte_eth_dev_count_avail();
    for (int portid = 0; portid < port_num; portid++) {
        fprintf(stderr, "userpci: setting up iface %d\n", portid);
        int r = setup_iface(portid, mtu, rx_queues);
        if (r < 0) {
            goto error;
        }
    }

    fprintf(stderr, "userpci: all ifaces ready.\n");

    const static char* ready_msg = "OK";
    int r = write(ready_fd, ready_msg, strlen(ready_msg) + 1);
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
