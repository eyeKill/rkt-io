#define _BSD_SOURCE
#include <spdk_bench.h>

#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <time.h>


struct task {
    void * buf;
    uint64_t lba_count;
    struct spdk_ns_entry *ns_entry;
};

#define BUF_SIZE 4096 * 2
#define GIGABYTE 1024 * 1024 * 1024
#define MAX_QUEUE_DEPTH 511

unsigned int g_lba = 0;
unsigned int g_max_lba = 0;
unsigned int g_queue_depth = 0;

static void spdk_read(struct task *t);

static void read_completion_cb(void *ctx, const struct spdk_nvme_cpl *cpl) {
    spdk_read((struct task *)ctx);
}

static void spdk_read(struct task *t) {
    struct spdk_ns_entry *ns_entry = t->ns_entry;

    if (g_lba > g_max_lba) {
        g_queue_depth--;
        return;
    }

    int rc = spdk_nvme_ns_cmd_read(ns_entry->ns,
                                   ns_entry->qpairs[0],
                                   t->buf,
                                   g_lba,
                                   t->lba_count,
                                   read_completion_cb,
                                   t,
                                   0);

    if (rc != 0) {
        fprintf(stderr, "spdk_read() failed: %d\n", rc);
        exit(1);
    }
    g_lba += t->lba_count;
};

static void spdk_write(struct task *t);

static void write_completion_cb(void *ctx, const struct spdk_nvme_cpl *cpl) {
    spdk_write((struct task *)ctx);
}

static void spdk_write(struct task *t) {
    struct spdk_ns_entry *ns_entry = t->ns_entry;

    if (g_lba > g_max_lba) {
        g_queue_depth--;
        return;
    }

    int rc = spdk_nvme_ns_cmd_write(ns_entry->ns,
                                   ns_entry->qpairs[0],
                                   t->buf,
                                   g_lba,
                                   t->lba_count,
                                   write_completion_cb,
                                   t,
                                   0);

    if (rc != 0) {
        fprintf(stderr, "spdk_read() failed: %d\n", rc);
        exit(1);
    }
    g_lba += t->lba_count;
}

void run_spdk_bench(struct spdk_ns_entry *ns_entry) {
    struct task tasks[MAX_QUEUE_DEPTH];
    int sector_size = spdk_nvme_ns_get_extended_sector_size(ns_entry->ns);
    uint64_t size = spdk_nvme_ns_get_size(ns_entry->ns);
    const unsigned int gigabytes = 10;
    g_max_lba = gigabytes * GIGABYTE / sector_size;
    g_queue_depth = MAX_QUEUE_DEPTH;

    for (int i = 0; i < MAX_QUEUE_DEPTH; i++) {
        struct task *t = &tasks[i];
        t->lba_count = BUF_SIZE / sector_size;
        t->buf = spdk_dma_zmalloc(BUF_SIZE, BUF_SIZE, NULL);
        t->ns_entry = ns_entry;
        if (!t->buf) {
            fprintf(stderr, "spdk_dma_zmalloc() failed\n");
            exit(1);
        }
        assert(t->buf);
    }

    struct timespec tstart = { 0, 0 }, tend = { 0, 0 };
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    for (int i = 0; i < MAX_QUEUE_DEPTH; i++) {
        /* spdk_read(&tasks[i]); */
        spdk_write(&tasks[i]);
    }

    while (g_lba < g_max_lba || g_queue_depth != 0) {
        spdk_nvme_qpair_process_completions(ns_entry->qpairs[0], 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &tend);
    double seconds = ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec);
    printf("io took about %.5f seconds -> %.5f MiB/s\n", seconds, (g_max_lba * sector_size) / 1024 / 1024 / seconds);
}
