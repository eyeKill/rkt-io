#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "dpdk.h"
#include "spdk_context.h"

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "USAGE: %s pipe-fd uid port-num\n", argv[0]);
        return 1;
    }
    int pipe_fd = atoi(argv[1]);
    int uid = atoi(argv[2]);
    int port_num = atoi(argv[3]);
    int exitcode = 0;

    struct spdk_context ctx = {};
    if (spdk_initialize(&ctx, true) < 0) {
        goto error;
    };

    for (int portid = 0; portid < port_num; portid++) {
        int r = setup_iface(portid);
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

    #warning "Rewrite this in C for production code!. Call bash scripts from setuid is insecure for a reason"
    setuid(0);
    int r = system(cmd);
    free(cmd);

    if (r != 0) {
        fprintf(stderr, "%s: failed to chown dpdk files: %d\n", argv[0], r);
        goto error;
    }

    // child will eventually close this
    char byte;
    read(pipe_fd, &byte, 1);
    goto cleanup;

error:
    exitcode = 1;
cleanup:
    spdk_context_detach(&ctx);
    spdk_context_free(&ctx);
    fprintf(stderr, "%s: stop setuid-helper\n", argv[0]);
    return exitcode;
}
