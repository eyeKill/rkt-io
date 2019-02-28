#include <stdbool.h>

struct lkl_spdk_ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct lkl_spdk_ctrlr_entry	*next;
	char			name[1024];
};
struct lkl_spdk_ns_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
	struct lkl_spdk_ns_entry		*next;
	struct spdk_nvme_qpair	*qpair;
};

struct lkl_spdk_context {
    struct lkl_spdk_ctrlr_entry *controllers;
    struct lkl_spdk_ns_entry *namespaces;
};

int lkl_spdk_initialize(struct lkl_spdk_context* ctx, bool primary);
void lkl_spdk_cleanup(struct lkl_spdk_context* ctx);
