#include "spdk_context.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>

#include <spdk/env.h>
#include <spdk/nvme.h>
#include <spdk/stdinc.h>

// We call this function from inside the enclave to drain the I/O queue
// This is because I/O callbacks contain function pointers.
void spdk_context_detach(struct spdk_context *ctx) {
    assert(ctx != NULL);

    struct spdk_ns_entry *ns_entry = ctx->namespaces;
    while (ns_entry) {
        struct spdk_ns_entry *next = ns_entry->next;

        if (ns_entry->qpairs) {
            for (int i = 0; i < ns_entry->qpairs_num; i++) {
                if (!ns_entry->qpairs[i]) {
                    spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpairs[i]);
                }
            }
            free(ns_entry->qpairs);
        }
        ns_entry = next;
    }

    struct spdk_ctrlr_entry *ctrlr_entry = ctx->controllers;
    while (ctrlr_entry) {
        struct spdk_ctrlr_entry *next = ctrlr_entry->next;

        spdk_nvme_detach(ctrlr_entry->ctrlr);
        ctrlr_entry = next;
    }
}

// We call this function from outside of the enclave
void spdk_context_free(struct spdk_context *ctx) {
    assert(ctx != NULL);

    struct spdk_ns_entry *ns_entry = ctx->namespaces;
    while (ns_entry) {
        struct spdk_ns_entry *next = ns_entry->next;

        free(ns_entry);
        ns_entry = next;
    }

    struct spdk_ctrlr_entry *ctrlr_entry = ctx->controllers;
    while (ctrlr_entry) {
        struct spdk_ctrlr_entry *next = ctrlr_entry->next;
        free(ctrlr_entry);
        ctrlr_entry = next;
    }

    pthread_t thread_id = ctx->ctrlr_thread_id;
    if (thread_id && pthread_cancel(thread_id) == 0) {
        pthread_join(thread_id, NULL);
    }
}

// in case of an error we call both functions outside of the enclave
static void spdk_context_cleanup(struct spdk_context *ctx) {
    spdk_context_detach(ctx);
    spdk_context_free(ctx);
}

static bool probe_cb(void *ctx, const struct spdk_nvme_transport_id *trid,
                     struct spdk_nvme_ctrlr_opts *opts) {
    fprintf(stderr, "spdk: Attaching to %s\n", trid->traddr);

    return true;
}

static int register_ns(struct spdk_context *ctx, struct spdk_nvme_ctrlr *ctrlr,
                       struct spdk_nvme_ns *ns) {
    /*
     * spdk_nvme_ctrlr is the logical abstraction in SPDK for an NVMe
     *  controller.  During initialization, the IDENTIFY data for the
     *  controller is read using an NVMe admin command, and that data
     *  can be retrieved using spdk_nvme_ctrlr_get_data() to get
     *  detailed information on the controller.  Refer to the NVMe
     *  specification for more details on IDENTIFY for NVMe controllers.
     */
    const struct spdk_nvme_ctrlr_data *cdata = spdk_nvme_ctrlr_get_data(ctrlr);

    if (!spdk_nvme_ns_is_active(ns)) {
        fprintf(stderr,
            "spdk: Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
            cdata->mn, cdata->sn, spdk_nvme_ns_get_id(ns));
        return 0;
    }

    struct spdk_ns_entry *entry = calloc(1, sizeof(struct spdk_ns_entry));
    if (entry == NULL) {
        return -ENOMEM;
    }

    entry->ctrlr = ctrlr;
    entry->ns = ns;
    entry->next = ctx->namespaces;
    ctx->namespaces = entry;

    fprintf(stderr, "spdk:  Namespace ID: %d size: %juGB\n",
            spdk_nvme_ns_get_id(ns), spdk_nvme_ns_get_size(ns) / 1000000000);
    return 0;
}

static void attach_cb(void *_ctx, const struct spdk_nvme_transport_id *trid,
                      struct spdk_nvme_ctrlr *ctrlr,
                      const struct spdk_nvme_ctrlr_opts *opts) {
    struct spdk_context *ctx = (struct spdk_context *)_ctx;
    const struct spdk_nvme_ctrlr_data *cdata = spdk_nvme_ctrlr_get_data(ctrlr);

    struct spdk_ctrlr_entry *entry = malloc(sizeof(struct spdk_ctrlr_entry));
    if (entry == NULL) {
        ctx->attach_error = -ENOMEM;
        fprintf(stderr, "spdk: spdk_ctrlr_entry malloc failed");
        return;
    }

    fprintf(stderr, "Attached to %s\n", trid->traddr);

    snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn,
             cdata->sn);

    entry->ctrlr = ctrlr;
    entry->next = ctx->controllers;
    ctx->controllers = entry;

    /*
     * Each controller has one or more namespaces.  An NVMe namespace is
     * basically equivalent to a SCSI LUN.  The controller's IDENTIFY data tells
     * us how many namespaces exist on the controller.  For Intel(R) P3X00
     * controllers, it will just be one namespace.
     *
     * Note that in NVMe, namespace IDs start at 1, not 0.
     */
    int num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
    fprintf(stderr, "spdk: Using controller %s with %d namespaces.\n",
            entry->name, num_ns);
    for (int nsid = 1; nsid <= num_ns; nsid++) {
        struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
        if (ns == NULL) {
            continue;
        }
        int r = register_ns(ctx, ctrlr, ns);
        if (r < 0) {
            ctx->attach_error = r;
            return;
        }
    }
}

static int register_qpairs(struct spdk_context *ctx) {
    // uint32_t cores =  get_nprocs();
    uint32_t cores = 1;

    struct spdk_ns_entry *ns_entry = ctx->namespaces;
    while (ns_entry != NULL) {
        /*
         * Allocate an I/O qpair that we can use to submit read/write requests
         *  to namespaces on the controller.  NVMe controllers typically support
         *  many qpairs per controller.  Any I/O qpair allocated for a
         * controller can submit I/O to any namespace on that controller.
         *
         * The SPDK NVMe driver provides no synchronization for qpair accesses -
         *  the application must ensure only a single thread submits I/O to a
         *  qpair, and that same thread must also check for completions on that
         *  qpair.  This enables extremely efficient I/O processing by making
         * all I/O operations completely lockless.
         */
        ns_entry->qpairs = calloc(cores, sizeof(struct spdk_nvme_qpair *));
        if (!ns_entry->qpairs) {
            fprintf(stderr, "spdk: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
            return -ENOMEM;
        }
        ns_entry->qpairs_num = cores;
        for (unsigned i = 0; i < cores; i++) {
            ns_entry->qpairs[i] =
                spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0);
            if (!ns_entry->qpairs[i]) {
                fprintf(stderr,
                        "spdk: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
                return -ENOMEM;
            }
        }

        ns_entry = ns_entry->next;
    }

    return 0;
}

static void *poll_ctrlrs(void *arg) {
    struct spdk_context *ctx = (struct spdk_context *)arg;
    assert(ctx);
    struct spdk_ctrlr_entry *controllers = ctx->controllers;

    spdk_unaffinitize_thread();

    /* Loop until the thread is cancelled */
    while (true) {
        int oldstate = 0;
        int rc = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
        if (rc != 0) {
            fprintf(
                stderr,
                "Unable to set cancel state disabled on g_init_thread: %s\n",
                strerror(-rc));
        }

        while (controllers) {
            spdk_nvme_ctrlr_process_admin_completions(controllers->ctrlr);
            controllers = controllers->next;
        }

        rc = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
        if (rc != 0) {
            fprintf(stderr,
                    "Unable to set cancel state enabled on g_init_thread: %s\n",
                    strerror(-rc));
        }

        /* This is a pthread cancellation point and cannot be removed. */
        sleep(1);
    }

    return NULL;
}

int spdk_initialize(struct spdk_context *ctx, bool primary_proc) {
    assert(ctx != NULL);

    struct spdk_env_opts opts;
    spdk_env_opts_init(&opts);
    opts.name = "sgxlkl";
    opts.shm_id = 0;
    opts.mem_channel = 1;
    opts.core_mask = "1";
    opts.proc_type = primary_proc ? PROC_TYPE_PRIMARY : PROC_TYPE_SECONDARY;

    int r = spdk_env_init(&opts);
    if (r < 0) {
        fprintf(stderr, "spdk: Unable to initialize SPDK env: %s\n",
                strerror(-r));
        return r;
    }

    r = spdk_nvme_probe(NULL, ctx, probe_cb, attach_cb, NULL);
    if (r != 0) {
        fprintf(stderr, "spdk: spdk_nvme_probe() failed: %s\n", strerror(-r));
        spdk_context_cleanup(ctx);
        return r;
    }
    if (ctx->attach_error) {
        spdk_context_cleanup(ctx);
        return r;
    }

    if (!primary_proc) {
        r = pthread_create(&ctx->ctrlr_thread_id, NULL, &poll_ctrlrs, ctx);
        if (r != 0) {
            spdk_context_cleanup(ctx);
            fprintf(stderr,
                    "spdk: Unable to spawn a thread to poll admin queues: %s\n",
                    strerror(-r));
            return r;
        }
        r = register_qpairs(ctx);
        if (r < 0) {
            spdk_context_cleanup(ctx);
            return r;
        }
    }

    return 0;
}
