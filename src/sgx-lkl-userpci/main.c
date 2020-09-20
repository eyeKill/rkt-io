#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/sysinfo.h>

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
    char *rx_queues_str = getenv("SGXLKL_DPDK_RX_QUEUES");
    char *ethreads_str = getenv("SGXLKL_ETHREADS");
    int mtu = 1500;
    size_t rx_queues = 1;
    size_t tx_queues = get_nprocs();

    if (mtustr) {
        mtu = atoi(mtustr);
    }
    if (rx_queues_str) {
        rx_queues = atoi(rx_queues_str);
    }
    if (ethreads_str) {
        tx_queues = atoi(ethreads_str);
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
        int r = setup_iface(portid, mtu, tx_queues, rx_queues);
        if (r < 0) {
            goto error;
        }
    }

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
