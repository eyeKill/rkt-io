#include "spdk.h"

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/env.h>


void lkl_spdk_cleanup(struct lkl_spdk_context* ctx)
{
    assert(ctx != NULL);

	struct lkl_spdk_ns_entry *ns_entry = ctx->namespaces;
	while (ns_entry) {
		struct lkl_spdk_ns_entry *next = ns_entry->next;
		free(ns_entry);
		ns_entry = next;
	}

	struct lkl_spdk_ctrlr_entry *ctrlr_entry = ctx->controllers;
	while (ctrlr_entry) {
		struct lkl_spdk_ctrlr_entry *next = ctrlr_entry->next;

		spdk_nvme_detach(ctrlr_entry->ctrlr);
		free(ctrlr_entry);
		ctrlr_entry = next;
	}
}

static bool probe_cb(void *ctx,
                     const struct spdk_nvme_transport_id *trid,
                     struct spdk_nvme_ctrlr_opts *opts)
{
	printf("Attaching to %s\n", trid->traddr);

	return true;
}

static void register_ns(struct lkl_spdk_context *ctx,
                        struct spdk_nvme_ctrlr *ctrlr,
                        struct spdk_nvme_ns *ns)
{
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
		printf("Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
		       cdata->mn, cdata->sn,
		       spdk_nvme_ns_get_id(ns));
		return;
	}

    struct lkl_spdk_ns_entry *entry = malloc(sizeof(struct lkl_spdk_ns_entry));
	if (entry == NULL) {
		perror("ns_entry malloc");
		exit(1);
	}

	entry->ctrlr = ctrlr;
	entry->ns = ns;
	entry->next = ctx->namespaces;
	ctx->namespaces = entry;

	printf("  Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns),
	       spdk_nvme_ns_get_size(ns) / 1000000000);
}

static void attach_cb(void *_ctx,
                      const struct spdk_nvme_transport_id *trid,
                      struct spdk_nvme_ctrlr *ctrlr,
                      const struct spdk_nvme_ctrlr_opts *opts)
{
    struct lkl_spdk_context *ctx = (struct lkl_spdk_context*) _ctx;
	const struct spdk_nvme_ctrlr_data *cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	struct lkl_spdk_ctrlr_entry *entry = malloc(sizeof(struct lkl_spdk_ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry malloc");
		exit(1);
	}

	printf("Attached to %s\n", trid->traddr);

	snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);

	entry->ctrlr = ctrlr;
	entry->next = ctx->controllers;
	ctx->controllers = entry;

	/*
	 * Each controller has one or more namespaces.  An NVMe namespace is basically
	 *  equivalent to a SCSI LUN.  The controller's IDENTIFY data tells us how
	 *  many namespaces exist on the controller.  For Intel(R) P3X00 controllers,
	 *  it will just be one namespace.
	 *
	 * Note that in NVMe, namespace IDs start at 1, not 0.
	 */
	int num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	printf("Using controller %s with %d namespaces.\n", entry->name, num_ns);
	for (int nsid = 1; nsid <= num_ns; nsid++) {
		struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL) {
			continue;
		}
		register_ns(ctx, ctrlr, ns);
	}
}

int lkl_spdk_initialize(struct lkl_spdk_context *ctx, bool primary)
{
    assert(ctx != NULL);

    struct spdk_env_opts opts;
	spdk_env_opts_init(&opts);
	opts.name = "sgxlkl";
	opts.shm_id = 0;
    opts.mem_channel = 1;
    opts.core_mask = "1";
    opts.proc_type = primary ? PROC_TYPE_PRIMARY : PROC_TYPE_SECONDARY;

    int r = spdk_env_init(&opts);
	if (r < 0) {
		fprintf(stderr, "spdk: Unable to initialize SPDK env: %s\n", strerror(-r));
		return r;
	}

    r = spdk_nvme_probe(NULL, &ctx, probe_cb, attach_cb, NULL);
    if (r != 0) {
    	fprintf(stderr, "spdk: spdk_nvme_probe() failed: %s\n", strerror(-r));
    	lkl_spdk_cleanup(ctx);
    }
    return r;
}
