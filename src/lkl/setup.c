/*
 * Copyright 2016, 2017, 2018 Imperial College London
 */

#include <assert.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/random.h>
#include <linux/module.h>
#include <stdio.h>
#include <stdlib.h>
#define _GNU_SOURCE // Needed for strchrnul
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#include <time.h>
#include <lkl.h>
#include <lkl_host.h>
#include <arpa/inet.h>

#include "libcryptsetup.h"
#include "libdevmapper.h"
#include "lkl/disk.h"
#include "lkl/dpdk.h"
#include "lkl/spdk.h"
#include "lkl/posix-host.h"
#include "lkl/setup.h"
#include "lkl/virtio_net.h"
#include "pthread.h"
#include "enclave_cmd.h"
#include "sgx_enclave_config.h"
#include "sgxlkl_debug.h"
#include "sgxlkl_util.h"
#include "spdk_context.h"
#include "wireguard.h"
#include "wireguard_util.h"

#define BIT(x) (1ULL << x)

#define UMOUNT_DISK_TIMEOUT 2000

int sethostname(const char *, size_t);

unsigned long sgxlkl_heap_start = 0;
unsigned long sgxlkl_heap_end = 0;
int sgxlkl_trace_lkl_syscall = 0;
int sgxlkl_trace_internal_syscall = 0;
int sgxlkl_trace_mmap = 0;
int sgxlkl_trace_thread = 0;
int sgxlkl_use_host_network = 0;
int sgxlkl_use_tap_offloading = 0;
int sgxlkl_mtu = 0;
int sgxlkl_xts_proxy = 0;
int sgxlkl_gso_offload = 0;
int sgxlkl_chksum_offload = 0;
int sgxlkl_dpdk_zerocopy = 0;
int sgxlkl_spdk_zerocopy = 0;

extern struct timespec sgxlkl_app_starttime;

size_t num_disks = 0;
struct enclave_disk_config *disks;
static struct spdk_context *spdk_context = NULL;
static size_t num_spdk_devs = 0;
static struct spdk_dev *spdk_devs;
static size_t num_dpdk_ifaces = 0;
static struct enclave_dpdk_config *dpdk_ifaces;

static void lkl_add_disks(struct enclave_disk_config *disks, size_t num_disks) {
    for (size_t i = 0; i < num_disks; ++i) {
        if (disks[i].fd == -1)
            continue;

        SGXLKL_VERBOSE("Disk %zu: Disk encryption: %s\n", i, (disks[i].enc ? "ON" : "off"));
        SGXLKL_VERBOSE("Disk %zu: Disk is read-only: %s\n", i, (disks[i].ro ? "YES" : "no"));

        /* Set ops to NULL to use platform default ops */
        struct lkl_disk lkl_disk;
        lkl_disk.ops = NULL;
        lkl_disk.blk_ops = NULL;
        lkl_disk.fd = disks[i].fd;
        int disk_dev_id = lkl_disk_add(&lkl_disk);
        if (disk_dev_id < 0) {
            fprintf(stderr, "Error: unable to register disk %d, %s\n", i, lkl_strerror(disk_dev_id));
            exit(EXIT_FAILURE);
        }
    }
}

static int lkl_prestart_net(enclave_config_t* encl) {
    struct lkl_netdev *netdev = sgxlkl_register_netdev_fd(encl->net_fd, encl->wait_on_io_host_calls);
    if (netdev == NULL) {
        fprintf(stderr, "Error: unable to register netdev\n");
        exit(2);
    }
    char mac[6] = { 0xCA, 0xFE, 0x00, 0x00, 0x00, 0x01 };
    struct lkl_netdev_args netdev_args = {
        .mac = mac,
        .offload= 0,
    };

    if (sgxlkl_use_tap_offloading) {
        netdev->has_vnet_hdr = 1;
        // Host and guest can handle partial checksums
        netdev_args.offload = BIT(LKL_VIRTIO_NET_F_CSUM) | BIT(LKL_VIRTIO_NET_F_GUEST_CSUM);
        // Host and guest can handle TSOv4
        netdev_args.offload |= BIT(LKL_VIRTIO_NET_F_HOST_TSO4) | BIT(LKL_VIRTIO_NET_F_GUEST_TSO4);
        // Host and guest can handle TSOv6
        netdev_args.offload |= BIT(LKL_VIRTIO_NET_F_HOST_TSO6) | BIT(LKL_VIRTIO_NET_F_GUEST_TSO6);
        // Host can merge receive buffers
        netdev_args.offload |= BIT(LKL_VIRTIO_NET_F_MRG_RXBUF);
    }

    int net_dev_id = lkl_netdev_add(netdev, &netdev_args);
    if (net_dev_id < 0) {
        fprintf(stderr, "Error: unable to register netdev, %s\n",
            lkl_strerror(net_dev_id));
        exit(net_dev_id);
    }

    return net_dev_id;
}

static void lkl_prepare_rootfs(const char* dirname, int perm) {
    int err = lkl_sys_access(dirname, /*LKL_S_IRWXO*/ F_OK);
    if (err < 0) {
        if (err == -LKL_ENOENT)
            err = lkl_sys_mkdir(dirname, perm);
        if (err < 0) {
            fprintf(stderr, "Error: Unable to mkdir %s: %s\n",
                dirname, lkl_strerror(err));
            exit(err);
        }
    }
}

static void lkl_copy_blkdev_nodes(const char* srcdir, const char* dstdir) {
    int err = 0;
    struct lkl_dir *dir = lkl_opendir(srcdir, &err);
    if (dir == NULL || err != 0) {
        fprintf(stderr, "Error: unable to opendir(%s)\n", srcdir);
        exit(err == 0 ? 1 : err);
    }

    char srcbuf[512] = {0};
    char dstbuf[512] = {0};
    strncpy(srcbuf, srcdir, sizeof(srcbuf));
    strncpy(dstbuf, dstdir, sizeof(dstbuf));
    int srcdir_len = strlen(srcbuf);
    int dstdir_len = strlen(dstbuf);
    if (srcbuf[srcdir_len-1] != '/')
        srcbuf[srcdir_len++] = '/';
    if (dstbuf[dstdir_len-1] != '/')
        dstbuf[dstdir_len++] = '/';
    struct lkl_linux_dirent64 *dev = NULL;
    int disknum = 0;
    while ((dev = lkl_readdir(dir)) != NULL) {
        strncpy(srcbuf+srcdir_len, dev->d_name, sizeof(srcbuf)-srcdir_len);
        strncpy(dstbuf+dstdir_len, dev->d_name, sizeof(dstbuf)-dstdir_len);
        struct lkl_stat stat;
        err = lkl_sys_stat(srcbuf, &stat);
        if (err != 0) {
            fprintf(stderr, "Error: lkl_sys_stat(%s) %s\n",
                srcbuf, lkl_strerror(err));
            exit(err);
        }
        if (!LKL_S_ISBLK(stat.st_mode))
            continue;

        lkl_sys_unlink(dstbuf);
        err = lkl_sys_mknod(dstbuf, LKL_S_IFBLK | 0600, stat.st_rdev);
        if (err != 0) {
            fprintf(stderr, "Error: lkl_sys_mknod(%s) %s\n",
                dstbuf, lkl_strerror(err));
            exit(err);
        }
    }
    err = lkl_errdir(dir);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_readdir(%s) = %d\n", srcdir, err);
        exit(err);
    }

    err = lkl_closedir(dir);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_closedir(%s) = %d\n", srcdir, err);
        exit(err);
    }
}

static void lkl_prestart_dpdk(enclave_config_t *encl) {
    num_dpdk_ifaces = encl->num_dpdk_ifaces;
    dpdk_ifaces = encl->dpdk_ifaces;

    for (size_t i = 0; i < num_dpdk_ifaces; i++) {
        fprintf(stderr, "Registering %d dpdk iface\n", i);
        struct enclave_dpdk_config *dpdk = &dpdk_ifaces[i];
        int ifindex = sgxlkl_register_dpdk_device(dpdk);

        if (ifindex < 0) {
            fprintf(stderr, "Error: unable to register netdev, %s\n",
                    lkl_strerror(ifindex));
            exit(ifindex);
        }
        dpdk->ifindex = ifindex;
    }
}

static void lkl_poststart_dpdk(enclave_config_t* encl) {

    for (size_t i = 0; i < num_dpdk_ifaces; i++) {
        struct enclave_dpdk_config *dpdk = &dpdk_ifaces[i];
        int res = 0;
        int ifindex = dpdk->ifindex;

        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET, &dpdk->net_ip4.s_addr, ip, INET_ADDRSTRLEN);
        SGXLKL_VERBOSE("dpdk iface addr: %s/%d\n", ip, dpdk->net_mask4);
        inet_ntop(AF_INET6, &dpdk->net_ip6.s6_addr, ip, INET6_ADDRSTRLEN);
        SGXLKL_VERBOSE("dpdk iface addr: %s/%d\n", ip, dpdk->net_mask6);
        SGXLKL_VERBOSE("dpdk iface mtu: %u\n", dpdk->mtu);

        res = lkl_if_set_ipv4(ifindex, dpdk->net_ip4.s_addr, dpdk->net_mask4);
        if (res < 0) {
          fprintf(stderr, "Error: lkl_if_set_ipv4(): %s\n", lkl_strerror(res));
          exit(res);
        }

        if (dpdk->mtu) {
            lkl_if_set_mtu(ifindex, dpdk->mtu);
        }

        res = lkl_if_up(ifindex);
        if (res < 0) {
          fprintf(stderr, "Error: lkl_if_up(eth0): %s\n", lkl_strerror(res));
          exit(res);
        }

        //res = lkl_if_set_ipv6(ifindex, dpdk->net_ip6.s6_addr, dpdk->net_mask6);
        //if (res < 0) {
        //  fprintf(stderr, "Error: lkl_if_set_ipv6(): %s\n", lkl_strerror(res));
        //  exit(res);
        //}
    }
}

static void lkl_stop_dpdk(void) {
    for (size_t i = 0; i < num_dpdk_ifaces; i++) {
        struct enclave_dpdk_config *dpdk = &dpdk_ifaces[i];
    }
}

static void lkl_mount_devtmpfs(const char* mntpoint) {
    int err = lkl_sys_mount("devtmpfs", (char*) mntpoint, "devtmpfs", 0, NULL);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(devtmpfs): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mount_shmtmpfs() {
    int err = lkl_sys_mount("tmpfs", "/dev/shm", "tmpfs", 0, "rw,nodev");
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(tmpfs) (/dev/shm): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mount_tmpfs() {
    int err = lkl_sys_mount("tmpfs", "/tmp", "tmpfs", 0, "mode=0777");
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(tmpfs): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mount_mntfs() {
    int err = lkl_sys_mount("tmpfs", "/mnt", "tmpfs", 0, "mode=0777");
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(tmpfs): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mount_sysfs() {
    int err = lkl_sys_mount("none", "/sys", "sysfs", 0, NULL);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(sysfs): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mount_runfs() {
    int err = lkl_sys_mount("tmpfs", "/run", "tmpfs", 0, "mode=0700");
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(tmpfs): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mount_procfs() {
    int err = lkl_sys_mount("proc", "/proc", "proc", 0, NULL);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mount(procfs): %s\n",
            lkl_strerror(err));
        exit(err);
    }
}

static void lkl_mknods() {
    lkl_sys_unlink("/dev/null");
    int err = lkl_sys_mknod("/dev/null", LKL_S_IFCHR | 0666, LKL_MKDEV(1,3));
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mknod(/dev/null) %s\n",
            lkl_strerror(err));
        exit(err);
    }
    lkl_sys_unlink("/dev/zero");
    err = lkl_sys_mknod("/dev/zero", LKL_S_IFCHR | 0666, LKL_MKDEV(1,5));
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mknod(/dev/zero) %s\n",
                lkl_strerror(err));
        exit(err);
    }
    lkl_sys_unlink("/dev/random");
    err = lkl_sys_mknod("/dev/random", LKL_S_IFCHR | 0444, LKL_MKDEV(1,8));
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mknod(/dev/random) %s\n",
                lkl_strerror(err));
        exit(err);
    }
    lkl_sys_unlink("/dev/urandom");
    err = lkl_sys_mknod("/dev/urandom", LKL_S_IFCHR | 0444, LKL_MKDEV(1,9));
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_mknod(/dev/urandom) %s\n",
                lkl_strerror(err));
        exit(err);
    }
}

static int lkl_mount_blockdev(const char* dev_str, const char* mnt_point,
                       const char *fs_type, int flags, const char* data) {
    char _data[4096];
    int err;

    err = lkl_sys_access("/mnt", LKL_S_IRWXO);
    if (err < 0) {
        if (err == -LKL_ENOENT)
            err = lkl_sys_mkdir("/mnt", 0700);
        if (err < 0)
            goto fail;
    }

    err = lkl_sys_mkdir(mnt_point, 0700);
    if (err < 0)
        goto fail;

    if (data) {
        strncpy(_data, data, sizeof(_data));
        _data[sizeof(_data) - 1] = 0;
    } else {
        _data[0] = 0;
    }

    err = lkl_sys_mount((char*)dev_str, (char*)mnt_point, (char*)fs_type, flags, _data);
    if (err < 0) {
        lkl_sys_rmdir(mnt_point);
        goto fail;
    }

fail:
    return err;
}

struct lkl_crypt_device {
    char *disk_path;
    char *crypt_name;
    int readonly;
    struct enclave_disk_config *disk_config;
};

static void* lkl_activate_crypto_disk_thread(struct lkl_crypt_device* lkl_cd) {
    int err;

    char* disk_path = lkl_cd->disk_path;

    struct crypt_device *cd;
    err = crypt_init(&cd, disk_path);
    if (err != 0) {
        fprintf(stderr, "Error: crypt_init(): %s (%d)\n", strerror(err), err);
        exit(err);
    }

    err = crypt_load(cd, CRYPT_LUKS, NULL);
    if (err != 0) {
        fprintf(stderr, "Error: crypt_load(): %s (%d)\n", strerror(err), err);
        exit(err);
    }

    char *key_outside = lkl_cd->disk_config->key;
    lkl_cd->disk_config->key = (char *) lkl_sys_mmap(NULL, lkl_cd->disk_config->key_len, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if ((int64_t) lkl_cd->disk_config->key <= 0) {
        fprintf(stderr, "Error: Unable to allocate memory for disk encryption key inside the enclave: %s\n", lkl_strerror((int) lkl_cd->disk_config->key));
        exit(EXIT_FAILURE);
    }
    memcpy(lkl_cd->disk_config->key, key_outside, lkl_cd->disk_config->key_len);

    err = crypt_activate_by_passphrase(cd, lkl_cd->crypt_name, CRYPT_ANY_SLOT, lkl_cd->disk_config->key, lkl_cd->disk_config->key_len, lkl_cd->readonly ? CRYPT_ACTIVATE_READONLY : 0);
    if (err == -1) {
        fprintf(stderr, "Error: Unable to activate encrypted disk. Please ensure you have provided the correct passphrase/keyfile!\n");
        exit(err);
    } else if (err != 0) {
        fprintf(stderr, "Error: Unable to activate encrypted disk due to unknown error (error code: %d)\n", err);
        exit(err);
    }

    crypt_free(cd);

    // The key is only needed during activation, so don't keep it around
    // afterwards and free up space.
    memset(lkl_cd->disk_config->key, 0, lkl_cd->disk_config->key_len);

    unsigned long munmap_ret;
    if((munmap_ret = lkl_sys_munmap((unsigned long) lkl_cd->disk_config->key, lkl_cd->disk_config->key_len))) {
        fprintf(stderr, "Error: Unable to unmap memory for disk encryption key: %s\n", lkl_strerror((int) munmap_ret));
        exit(EXIT_FAILURE);
    }
    lkl_cd->disk_config->key = NULL;
    lkl_cd->disk_config->key_len = 0;

    return 0;
}

static void* lkl_activate_verity_disk_thread(struct lkl_crypt_device* lkl_cd) {
    int err;

    char* disk_path = lkl_cd->disk_path;

    struct crypt_device *cd;
    // cryptsetup!
    err = crypt_init(&cd, disk_path);
    if (err != 0) {
        fprintf(stderr, "Error: crypt_init(): %s (%d)\n", strerror(err), err);
        exit(err);
    }

    /*
     * The dm-verity Merkle tree that contains the hashes of all data blocks is
     * stored on the disk image following the actual data blocks. The offset that
     * signifies both the end of the data region as well as the start of the hash
     * region has to be provided to SGX-LKL.
     */
    struct crypt_params_verity verity_params = {
        .data_device = disk_path,
        .hash_device = disk_path,
        .hash_area_offset = lkl_cd->disk_config->roothash_offset,
        .data_size = lkl_cd->disk_config->roothash_offset / 512, // In blocks, divide by block size
        .data_block_size = 512,
        .hash_block_size = 512,
        .hash_name = "sha256"
    };

    err = crypt_load(cd, CRYPT_VERITY, &verity_params);
    if (err != 0) {
        fprintf(stderr, "Error: crypt_load(): %s (%d)\n", strerror(err), err);
        exit(err);
    }

    char* volume_hash_bytes = NULL;
    ssize_t hash_size = crypt_get_volume_key_size(cd);
    if (hex_to_bytes(lkl_cd->disk_config->roothash, &volume_hash_bytes) != hash_size) {
        fprintf(stderr, "Invalid root hash string specified!\n");
        exit(1);
    }

    err = crypt_activate_by_volume_key(cd, "verityroot", volume_hash_bytes, 32, lkl_cd->readonly ? CRYPT_ACTIVATE_READONLY : 0);
    if (err != 0) {
        fprintf(stderr, "Error: crypt_activate_by_volume_key(): %s (%d)\n", strerror(err), err);
        exit(err);
    }

    crypt_free(cd);
    free(volume_hash_bytes);

    return NULL;
}

static void lkl_run_in_kernel_stack(void *(*start_routine) (void *), void* arg) {
    int err;

    /*
     * We need to pivot to a stack which is inside LKL's known memory mappings
     * otherwise get_user_pages will not manage to find the mapping, and will
     * fail.
     *
     * Buffers passed to the kernel via the crypto API need to be allocated
     * on this stack, or on heap pages allocated via lkl_sys_mmap.
     */
    const int stack_size = 32*1024;

    void* addr = lkl_sys_mmap(NULL, stack_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Error: lkl_sys_mmap failed\n");
        exit(1);
    }

    pthread_t pt;
    pthread_attr_t ptattr;
    pthread_attr_init(&ptattr);
    pthread_attr_setstack(&ptattr, addr, stack_size);
    err = pthread_create(&pt, &ptattr, start_routine, arg);
    if (err < 0) {
        fprintf(stderr, "Error: pthread_create()=%s (%d)\n", strerror(err), err);
        exit(err);
    }

    err = pthread_join(pt, NULL);
    if (err < 0) {
        fprintf(stderr, "Error: pthread_join()=%s (%d)\n", strerror(err), err);
        exit(err);
    }
}

#define DEV_PATH_LEN 20

static int device_path(int dev_id, char* dev) {
    // We assign dev paths from /dev/vda to /dev/vdz, assuming we won't need
    // support for more than 26 disks.
    if ('a' + (dev_id - 1) > 'z') {
        fprintf(stderr, "Error: Too many disks (maximum is 26). Failed to mount disk %d at %s.\n", dev_id);
        // Adjust number to number of mounted disks.
        num_disks = 26;
        return 1;
    }
    snprintf(dev, DEV_PATH_LEN, "/dev/vd%c", 'a' + dev_id - 1);
    return 0;
}

static void spdk_mountpoint(int dev_id, char* dev) {
    snprintf(dev, DEV_PATH_LEN, "/mnt/spdk%d", dev_id);
}
extern void *g_spdk_nvme_driver;

static void lkl_start_spdk(struct spdk_context *ctx) {

    for (struct spdk_ns_entry *ns_entry = ctx->namespaces; ns_entry; ns_entry = ns_entry->next) {
        num_spdk_devs++;
    }
    spdk_devs = (struct spdk_dev*) calloc(num_spdk_devs, sizeof(struct spdk_dev));
    if (!spdk_devs) {
        fprintf(stderr, "Error: unable to allocate memory spdk devices\n");
        exit(1);
    }
    g_spdk_nvme_driver = ctx->spdk_nvme_driver;

    size_t idx = 0;

    for (struct spdk_ns_entry *ns_entry = ctx->namespaces; ns_entry; ns_entry = ns_entry->next) {
        struct spdk_dev *dev = &spdk_devs[idx];
        memcpy(&dev->ns_entry, ns_entry, sizeof(struct spdk_ns_entry));
        int rc = sgxlkl_register_spdk_device(dev);
        if (rc < 0) {
            fprintf(stderr, "Error: unable to register spdk devices\n");
            exit(1);
        }
        char dev_raw_path[DEV_PATH_LEN], cryptname[DEV_PATH_LEN - 5], dev_enc_path[DEV_PATH_LEN], mnt[DEV_PATH_LEN];
        char *dev_path = dev_raw_path;

        snprintf(dev_raw_path, sizeof(dev_raw_path), "/dev/spdk%d", spdk_devs[idx].dev_id);

        if (ctx->key) {
          struct lkl_crypt_device lkl_cd;
          struct enclave_disk_config disk_config;
          disk_config.key = ctx->key;
          disk_config.key_len = ctx->key_len;

          snprintf(cryptname, sizeof(cryptname), "spdk%d", spdk_devs[idx].dev_id);
          lkl_cd.crypt_name = cryptname;
          lkl_cd.disk_path = dev_raw_path;
          lkl_cd.readonly = 0;
          lkl_cd.disk_config = &disk_config;
          lkl_run_in_kernel_stack((void * (*)(void *)) &lkl_activate_crypto_disk_thread, (void *) &lkl_cd);

          snprintf(dev_enc_path, sizeof(dev_enc_path), "/dev/mapper/spdk%d", spdk_devs[idx].dev_id);
          dev_path = dev_enc_path;
        }

        spdk_mountpoint(spdk_devs[idx].dev_id, mnt);

        SGXLKL_VERBOSE("spdk: mount(%s, %s)\n", dev_path, mnt);
        rc = lkl_mount_blockdev(dev_path, mnt, "ext4", 0, NULL);
        if (rc < 0) {
            fprintf(stderr, "Error: lkl_mount_blockdev(%s, %s)=%s (%d)\n", dev_path, mnt, lkl_strerror(rc), rc);
            exit(rc);
        }
        idx++;
    }
}

static void lkl_stop_spdk() {
    for (size_t i = 0; i < num_spdk_devs; i++) {
        char mnt[DEV_PATH_LEN];
        spdk_mountpoint(spdk_devs[i].dev_id, mnt);
        int err = lkl_umount_timeout(mnt, 0, UMOUNT_DISK_TIMEOUT);
        if (err < 0) {
            fprintf(stderr, "Error: lkl_mount_umount(%s)=%s (%d)\n", mnt, lkl_strerror(err), err);
        }
        sgxlkl_unregister_spdk_device(&spdk_devs[i]);
    }
    free(spdk_devs);
    sgxlkl_stop_spdk();
}

static void lkl_mount_virtual() {
    lkl_mount_devtmpfs("/dev");
    lkl_prepare_rootfs("/proc", 0700);
    lkl_mount_procfs();
    lkl_prepare_rootfs("/sys", 0700);
    lkl_mount_sysfs();
    lkl_prepare_rootfs("/run", 0700);
    lkl_mount_runfs();
    lkl_mknods();
}

static void lkl_set_working_dir(const char* path) {
    SGXLKL_VERBOSE("Setting working directory: %s\n", path);
    char *copy = strdup(path);
    if (!copy) {
        fprintf(stderr, "Error: lkl_sys_chdir(%s): %s\n", path, lkl_strerror(errno));
    }
    int ret = lkl_sys_chdir(copy);
    free(copy);
    if (ret == 0) {
        return;
    }

    fprintf(stderr, "Error: lkl_sys_chdir(%s): %s\n", path, lkl_strerror(ret));
    exit(1);
}

static void lkl_mount_root_disk(struct enclave_disk_config *disk) {
    int err = 0;
    char mnt_point[] = {"/mnt/vda"};
    char dev_str_raw[] = {"/dev/vda"};

    char dev_str_enc[] = {"/dev/mapper/cryptroot"};
    char dev_str_verity[] = {"/dev/mapper/verityroot"};

    char *dev_str = dev_str_raw;
    char new_dev_str[] = {"/mnt/vda/dev/"};

    int lkl_trace_lkl_syscall_bak = sgxlkl_trace_lkl_syscall;
    int lkl_trace_internal_syscall_bak = sgxlkl_trace_internal_syscall;

    if ((sgxlkl_trace_lkl_syscall || sgxlkl_trace_internal_syscall) && (disk->roothash || disk->enc)) {
        sgxlkl_trace_lkl_syscall = 0;
        sgxlkl_trace_internal_syscall = 0;
        SGXLKL_VERBOSE("Disk encryption/integrity enabled: Temporarily disabling tracing.\n");
    }

    struct lkl_crypt_device lkl_cd;
    lkl_cd.crypt_name = "cryptroot";
    lkl_cd.disk_path = dev_str;
    lkl_cd.readonly = disk->ro;
    lkl_cd.disk_config = disk;

    if (disk->roothash != NULL) {
        lkl_run_in_kernel_stack((void * (*)(void *)) &lkl_activate_verity_disk_thread, (void *) &lkl_cd);

        // We now want to mount the verified volume
        dev_str = dev_str_verity;
        lkl_cd.disk_path = dev_str_verity;
        // dm-verity is read only
        disk->ro = 1;
        lkl_cd.readonly = 1;
    }
    if (disk->enc) {
        lkl_run_in_kernel_stack((void * (*)(void *)) &lkl_activate_crypto_disk_thread, (void *) &lkl_cd);

        // We now want to mount the decrypted volume
        dev_str = dev_str_enc;
    }

    if ((lkl_trace_lkl_syscall_bak && !sgxlkl_trace_lkl_syscall) || (lkl_trace_internal_syscall_bak && !sgxlkl_trace_internal_syscall)) {
        SGXLKL_VERBOSE("Devicemapper setup complete: reenabling lkl_strace\n");
        sgxlkl_trace_lkl_syscall = lkl_trace_lkl_syscall_bak;
        sgxlkl_trace_internal_syscall = lkl_trace_internal_syscall_bak;
    }

    err = lkl_mount_blockdev(dev_str, mnt_point, "ext4", disk->ro ? LKL_MS_RDONLY : 0, NULL);
    if (err < 0)
        sgxlkl_fail("Error: lkl_mount_blockdev()=%s (%d)\n", lkl_strerror(err), err);
    disk->mounted = 1;

    /* set up /dev in the new root */
    lkl_prepare_rootfs(new_dev_str, 0700);
    lkl_mount_devtmpfs(new_dev_str);
    lkl_copy_blkdev_nodes("/dev/", new_dev_str);

    /* clean up */
    lkl_sys_umount("/proc", 0);
    lkl_sys_umount("/sys", 0);
    lkl_sys_umount("/run", 0);
    lkl_sys_umount("/dev", 0);

    /* pivot */
    err = lkl_sys_chroot(mnt_point);
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_chroot(%s): %s\n",
            mnt_point, lkl_strerror(err));
        exit(err);
    }

    err = lkl_sys_chdir("/");
    if (err != 0) {
        fprintf(stderr, "Error: lkl_sys_chdir(%s): %s\n",
            mnt_point, lkl_strerror(err));
        exit(err);
    }

    lkl_prepare_rootfs("/dev", 0700);
    lkl_prepare_rootfs("/dev/shm", 0777);
    lkl_prepare_rootfs("/mnt", 0700);
    lkl_prepare_rootfs("/tmp", 0777);
    lkl_prepare_rootfs("/sys", 0700);
    lkl_prepare_rootfs("/run", 0700);
    lkl_prepare_rootfs("/proc", 0700);
    lkl_mount_shmtmpfs();
    lkl_mount_tmpfs();
    lkl_mount_mntfs();
    lkl_mount_sysfs();
    lkl_mount_runfs();
    lkl_mount_procfs();
}

void lkl_mount_disks(struct enclave_disk_config* _disks, size_t _num_disks, const char *cwd) {
    num_disks = _num_disks;
    if (num_disks <= 0)
        sgxlkl_fail("No root disk provided. Aborting...\n");

    // We copy the disk config as we need to keep track of mount paths and can't
    // rely on the enclave_config to be around and unchanged for the lifetime of
    // the enclave.
    // (Decryption keys are copied in lkl_activate_crypto_thread)
    disks = (struct enclave_disk_config*) malloc(sizeof(struct enclave_disk_config) * num_disks);
    memcpy(disks, _disks, sizeof(struct enclave_disk_config) * num_disks);

    lkl_add_disks(disks, num_disks);

    // Find root disk
    enclave_disk_config_t *root_disk = NULL;
    for (size_t i = 0; i < num_disks; ++i) {
        if (!strcmp(disks[i].mnt, "/")) {
            root_disk = &disks[i];
            break;
        }
    }

    if (!root_disk) {
        sgxlkl_fail("No root disk (mount point '/') provided.\n");
    }

    lkl_mount_root_disk(root_disk);

    char dev_path[] = { "/dev/vdXX" };
    size_t dev_path_len = strlen(dev_path);
    for (size_t i = 0; i < num_disks; ++i) {
        char dev_path[DEV_PATH_LEN];
        if (device_path(i + 1, dev_path) < 0) {
            exit(1);
        }
        if (root_disk == &disks[i] || disks[i].fd == -1)
            continue;

        // We assign dev paths from /dev/vda to /dev/vdz, assuming we won't need
        // support for more than 26 disks.
        if ('a' + i > 'z') {
            fprintf(stderr, "Error: Too many disks (maximum is 26). Failed to mount disk %d at %s.\n", i, disks[i].mnt);
            // Adjust number to number of mounted disks.
            num_disks = 26;
            return;
        }
        snprintf(dev_path, dev_path_len, "/dev/vd%c", 'a' + i);
        int err = lkl_mount_blockdev(dev_path, disks[i].mnt, "ext4", disks[i].ro ? LKL_MS_RDONLY : 0, NULL);
        if (err < 0)
            sgxlkl_fail("Error: lkl_mount_blockdev()=%s (%d)\n", lkl_strerror(err), err);
        disks[i].mounted = 1;
    }

    if (spdk_context) {
        lkl_start_spdk(spdk_context);
    }

    if (cwd) {
        lkl_set_working_dir(cwd);
    }
}

void lkl_poststart_net(enclave_config_t* encl, int net_dev_id) {
    int res = 0;
    if (net_dev_id >= 0) {
        int ifidx = lkl_netdev_get_ifindex(net_dev_id);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &encl->net_ip4.s_addr, ip, INET_ADDRSTRLEN);
        SGXLKL_VERBOSE("tap iface addr: %s/%d\n", ip, encl->net_mask4);
        res = lkl_if_set_ipv4(ifidx, encl->net_ip4.s_addr, encl->net_mask4);
        if (res < 0) {
            fprintf(stderr, "Error: lkl_if_set_ipv4(): %s\n",
                    lkl_strerror(res));
            exit(res);
        }

        res = lkl_if_up(ifidx);
        if (res < 0) {
            fprintf(stderr, "Error: lkl_if_up(eth0): %s\n",
                    lkl_strerror(res));
            exit(res);
        }

        if (sgxlkl_mtu) {
            lkl_if_set_mtu(ifidx, sgxlkl_mtu);
        }

        //if (encl->net_gw4.s_addr > 0) {
        //    res = lkl_set_ipv4_gateway(encl->net_gw4.s_addr);
        //    if (res < 0) {
        //        fprintf(stderr, "Error: lkl_set_ipv4_gateway(): %s\n",
        //                lkl_strerror(res));
        //        exit(res);
        //    }
        //}

        //if (encl->net_gw6.s6_addr > 0) {
        //  res = lkl_set_ipv6_gateway(encl->net_gw6.s6_addr);
        //  if (res < 0) {
        //    fprintf(stderr, "Error: lkl_set_ipv6_gateway(): %s\n",
        //            lkl_strerror(res));
        //    exit(res);
        //  }
        //}
    }
    res = lkl_if_up(1 /* loopback */);
    if (res < 0) {
        fprintf(stderr, "Error: lkl_if_up(1=lo): %s\n", lkl_strerror(res));
        exit(res);
    }
}

static void do_sysctl(enclave_config_t *encl) {
    if (!encl->sysctl)
        return;

    char *sysctl_all = strdup(encl->sysctl);
    char *sysctl = sysctl_all;
    while (*sysctl) {
        while (*sysctl == ' ')
            sysctl++;

        char *path = sysctl;
        char *val = strchrnul(path, '=');
        if (!*val) {
            sgxlkl_warn("Failed to set sysctl config \"%s\", key and value not seperated by '='.\n", path);
            break;
        }

        *val = '\0';
        val++;
        char *val_end = strchrnul(val, ';');
        if (*val_end) {
            *val_end = '\0';
            val_end++;
        }
        sysctl = val_end;

        SGXLKL_VERBOSE("Setting sysctl config: %s=%s\n", path, val);
        if (lkl_sysctl(path, val)) {
            sgxlkl_warn("Failed to set sysctl config %s=%s\n", path, val);
            break;
        }
    }

    free(sysctl_all);
}

static void init_wireguard(enclave_config_t *encl) {
    wg_device new_device = {
        .name = "wg0",
        .listen_port = encl->wg.listen_port,
        .flags = WGDEVICE_HAS_PRIVATE_KEY | WGDEVICE_HAS_LISTEN_PORT,
        .first_peer = NULL,
        .last_peer = NULL
    };

    char *wg_key_b64 = encl->wg.key;
    if (wg_key_b64) {
        wg_key_from_base64(new_device.private_key, wg_key_b64);
    } else {
        wg_generate_private_key(new_device.private_key);
    }

    wgu_add_peers(&new_device, encl->wg.peers, encl->wg.num_peers, 0);

    if (wg_add_device(new_device.name) < 0) {
        perror("Unable to add wireguard device");
        return;
    }

    if (wg_set_device(&new_device) < 0) {
        perror("Unable to set wireguard device");
        return;
    }

    int wgifindex = lkl_ifname_to_ifindex(new_device.name);
    lkl_if_set_ipv4(wgifindex, encl->wg.ip.s_addr, 24);
    lkl_if_up(wgifindex);
}

static void init_random() {
    struct rand_pool_info *pool_info = 0;
    FILE *f;
    int fd;

    SGXLKL_VERBOSE("Adding entropy to entropy pool.\n");

    char buf[8] = {0};
    f  = fopen("/proc/sys/kernel/random/poolsize", "r");
    if (!f)
        goto err;
    if (fgets(buf, 8, f) == NULL)
        goto err;
    // /proc/sys/kernel/random/poolsize for kernel 2.6+ contains pool size in
    // bits, divide by 8 for number of bytes.
    int poolsize = atoi(buf) / 8;

    // To be on the safe side, add entropy equivalent to the pool size.
    pool_info = (struct rand_pool_info *) malloc(sizeof(pool_info) + poolsize);
    if (!pool_info)
        goto err;

    pool_info->entropy_count = poolsize * 8;
    pool_info->buf_size = poolsize;

    uint64_t *entropy_buf = (uint64_t *) pool_info->buf;
    for (int i = 0; i < poolsize / 8; i++) {
        // TODO Use intrinsics
        // if (!_rdrand64_step(&entropy_buf[i]))
        //    goto err;
        register uint64_t rd;
        __asm__ volatile ( "rdrand %0;" : "=r" ( rd ) );
        entropy_buf[i] = rd;
    }

    fd = open("/dev/random", O_RDONLY);
    if (ioctl(fd, RNDADDENTROPY, pool_info) == -1)
        goto err;

    goto out;
err:
    fprintf(stderr, "[ SGX-LKL ] Warning: Failed to add entropy to entropy pool.\n");
out:
    if (f)
        fclose(f);
    if (fd)
        close(fd);
    if (pool_info)
        free(pool_info);
}

#ifdef X86MODULES
#define init_module(addr, length, param_values) syscall(SYS_init_module, addr, length, param_values)

// We are not using _binary_lkl_x86mods_*_ko_size as it seems to be unreliable.
// Instead use end - start to calculate size.
extern char _binary_lkl_x86mods_aes_x86_64_ko_start[];
extern char _binary_lkl_x86mods_aes_x86_64_ko_end[];
extern char _binary_lkl_x86mods_aesni_intel_ko_start[];
extern char _binary_lkl_x86mods_aesni_intel_ko_end[];
extern char _binary_lkl_x86mods_xtsproxy_ko_start[];
extern char _binary_lkl_x86mods_xtsproxy_ko_end[];
extern char _binary_lkl_x86mods_chacha20_x86_64_ko_start[];
extern char _binary_lkl_x86mods_chacha20_x86_64_ko_end[];
extern char _binary_lkl_x86mods_poly1305_x86_64_ko_start[];
extern char _binary_lkl_x86mods_poly1305_x86_64_ko_end[];
extern char _binary_lkl_x86mods_salsa20_x86_64_ko_start[];
extern char _binary_lkl_x86mods_salsa20_x86_64_ko_end[];
extern char _binary_lkl_x86mods_serpent_avx_x86_64_ko_start[];
extern char _binary_lkl_x86mods_serpent_avx_x86_64_ko_end[];
extern char _binary_lkl_x86mods_serpent_avx2_ko_start[];
extern char _binary_lkl_x86mods_serpent_avx2_ko_end[];
extern char _binary_lkl_x86mods_sha1_ssse3_ko_start[];
extern char _binary_lkl_x86mods_sha1_ssse3_ko_end[];
extern char _binary_lkl_x86mods_sha256_ssse3_ko_start[];
extern char _binary_lkl_x86mods_sha256_ssse3_ko_end[];
extern char _binary_lkl_x86mods_sha512_ssse3_ko_start[];
extern char _binary_lkl_x86mods_sha512_ssse3_ko_end[];
extern char _binary_lkl_x86mods_twofish_x86_64_ko_start[];
extern char _binary_lkl_x86mods_twofish_x86_64_ko_end[];
extern char _binary_lkl_x86mods_twofish_x86_64_3way_ko_start[];
extern char _binary_lkl_x86mods_twofish_x86_64_3way_ko_end[];
extern char _binary_lkl_x86mods_twofish_avx_x86_64_ko_start[];
extern char _binary_lkl_x86mods_twofish_avx_x86_64_ko_end[];

void load_x86_kernel_modules(void) {
    struct kmod_struct {
            const char *name; const char *addr; size_t size;
    };

    const struct kmod_struct kmods[] = {
      {"aes-x86_64.ko",           _binary_lkl_x86mods_aes_x86_64_ko_start,          (size_t) (_binary_lkl_x86mods_aes_x86_64_ko_end - _binary_lkl_x86mods_aes_x86_64_ko_start)},
      {"aesni-intel.ko",          _binary_lkl_x86mods_aesni_intel_ko_start,         (size_t) (_binary_lkl_x86mods_aesni_intel_ko_end - _binary_lkl_x86mods_aesni_intel_ko_start)},
      //{"chacha20-x86_64.ko",      _binary_lkl_x86mods_chacha20_x86_64_ko_start,     (size_t) (_binary_lkl_x86mods_chacha20_x86_64_ko_end - _binary_lkl_x86mods_chacha20_x86_64_ko_start)},
      //{"poly1305-x86_64.ko",      _binary_lkl_x86mods_poly1305_x86_64_ko_start,     (size_t) (_binary_lkl_x86mods_poly1305_x86_64_ko_end - _binary_lkl_x86mods_poly1305_x86_64_ko_start)},
      //{"salsa20-x86_64.ko",       _binary_lkl_x86mods_salsa20_x86_64_ko_start,      (size_t) (_binary_lkl_x86mods_salsa20_x86_64_ko_end - _binary_lkl_x86mods_salsa20_x86_64_ko_start)},

      //{"serpent-avx-x86_64.ko",   _binary_lkl_x86mods_serpent_avx_x86_64_ko_start,  (size_t) (_binary_lkl_x86mods_serpent_avx_x86_64_ko_end - _binary_lkl_x86mods_serpent_avx_x86_64_ko_start)},
      //{"serpent-avx2.ko",         _binary_lkl_x86mods_serpent_avx2_ko_start,        (size_t) (_binary_lkl_x86mods_serpent_avx2_ko_end - _binary_lkl_x86mods_serpent_avx2_ko_start)},
      //{"sha1-ssse3.ko",           _binary_lkl_x86mods_sha1_ssse3_ko_start,          (size_t) (_binary_lkl_x86mods_sha1_ssse3_ko_end - _binary_lkl_x86mods_sha1_ssse3_ko_start)},
      {"sha256-ssse3.ko",         _binary_lkl_x86mods_sha256_ssse3_ko_start,        (size_t) (_binary_lkl_x86mods_sha256_ssse3_ko_end - _binary_lkl_x86mods_sha256_ssse3_ko_start)},
      //{"sha512-ssse3.ko",         _binary_lkl_x86mods_sha512_ssse3_ko_start,        (size_t) (_binary_lkl_x86mods_sha512_ssse3_ko_end - _binary_lkl_x86mods_sha512_ssse3_ko_start)},
      //{"twofish-x86_64.ko",       _binary_lkl_x86mods_twofish_x86_64_ko_start,      (size_t) (_binary_lkl_x86mods_twofish_x86_64_ko_end - _binary_lkl_x86mods_twofish_x86_64_ko_start)},
      //{"twofish-x86_64-3way.ko",  _binary_lkl_x86mods_twofish_x86_64_3way_ko_start, (size_t) (_binary_lkl_x86mods_twofish_x86_64_3way_ko_end - _binary_lkl_x86mods_twofish_x86_64_3way_ko_start)},
      //{"twofish-avx-x86_64.ko",   _binary_lkl_x86mods_twofish_avx_x86_64_ko_start,  (size_t) (_binary_lkl_x86mods_twofish_avx_x86_64_ko_end - _binary_lkl_x86mods_twofish_avx_x86_64_ko_start)},
    };

    for (int i = 0; i < sizeof(kmods)/sizeof(kmods[0]); i++) {
        if (init_module(kmods[i].addr, kmods[i].size, "") != 0) {
            SGXLKL_VERBOSE("Failed to load kernel module %s: %s\n", kmods[i].name, strerror(errno));
        } else {
            SGXLKL_VERBOSE("Successfully loaded kernel module %s\n", kmods[i].name);
        }
    }

    const struct kmod_struct xtsproxy = {"xtsproxy.ko",             _binary_lkl_x86mods_xtsproxy_ko_start,         (size_t) (_binary_lkl_x86mods_xtsproxy_ko_end - _binary_lkl_x86mods_xtsproxy_ko_start)};

    if (sgxlkl_xts_proxy) {
        if (init_module(xtsproxy.addr, xtsproxy.size, "") != 0) {
            int printf(const char* f,...); printf("%s() at %s:%d\n", __func__, __FILE__, __LINE__); __asm__("int3; nop" ::: "memory");
            SGXLKL_VERBOSE("Failed to load kernel module %s: %s\n", xtsproxy.name, strerror(errno));
        } else {
            SGXLKL_VERBOSE("Successfully loaded kernel module %s\n", xtsproxy.name);
        }
    } else {
            SGXLKL_VERBOSE("Skip loading %s\n", xtsproxy.name);
    }
}
#endif /* X86MODULES */

void lkl_start_init(enclave_config_t* encl) {
    size_t i;

    // Provide LKL host ops and virtio block device ops
    lkl_host_ops = sgxlkl_host_ops;
    if (getenv_bool("SGXLKL_HD_MMAP", 0))
        lkl_dev_blk_ops = sgxlkl_dev_blk_mem_ops;
    else
        lkl_dev_blk_ops = sgxlkl_dev_blk_ops;

    // TODO Make tracing options configurable via SGX-LKL config file.
    if (getenv_bool("SGXLKL_TRACE_LKL_SYSCALL", 0))
        sgxlkl_trace_lkl_syscall = 1;

    if (getenv_bool("SGXLKL_TRACE_INTERNAL_SYSCALL", 0))
      sgxlkl_trace_internal_syscall = 1;

    if (getenv_bool("SGXLKL_TRACE_SYSCALL", 0)) {
      sgxlkl_trace_lkl_syscall = 1;
      sgxlkl_trace_internal_syscall = 1;
    }

    if (getenv_bool("SGXLKL_TRACE_MMAP", 0))
        sgxlkl_trace_mmap = 1;

    if (getenv_bool("SGXLKL_TRACE_THREAD", 0))
        sgxlkl_trace_thread = 1;

    sgxlkl_xts_proxy = encl->xts_proxy;
    sgxlkl_gso_offload = encl->gso_offload;
    sgxlkl_chksum_offload = encl->chksum_offload;
    sgxlkl_dpdk_zerocopy = encl->dpdk_zerocopy;
    sgxlkl_spdk_zerocopy = encl->spdk_zerocopy;

    if (encl->hostnet)
        sgxlkl_use_host_network = 1;

    if (encl->tap_offload)
        sgxlkl_use_tap_offloading = 1;

    sgxlkl_heap_start = (unsigned long)encl->heap;
    sgxlkl_heap_end = sgxlkl_heap_start + encl->heapsize;

    sgxlkl_mtu = encl->tap_mtu;

    lkl_setup_x86_cpu(encl->x86_vendor_id,
                      encl->x86_family,
                      encl->x86_model,
                      (char *)encl->x86_capabilities,
                      encl->x86_xfeature_mask);

    // Register network tap if given one
    int net_dev_id = -1;
    if (encl->net_fd != 0)
        net_dev_id = lkl_prestart_net(encl);

    // Start kernel threads (synchronous, doesn't return before kernel is ready)
    const char *lkl_cmdline = encl->kernel_cmd;
    SGXLKL_VERBOSE("Kernel command line: \"%s\"\n", lkl_cmdline);

    long res = lkl_start_kernel(&lkl_host_ops, lkl_cmdline);
    if (res < 0) {
        fprintf(stderr, "Error: could not start LKL kernel, %s\n",
            lkl_strerror(res));
        exit(res);
    }
    SGXLKL_VERBOSE("LKL kernel started\n");

    // Open dummy files to use LKL's 0/1/2 file descriptors
    // (otherwise they will be assigned to the app's first fopen()s
    // and become undistinguishable from STDIN/OUT/ERR)
    for (int i = 0; i < 3; i++) {
        int err = 0;
        lkl_opendir("/", &err);
        if (err != 0) {
            fprintf(stderr, "Error: unable to pad file descriptor table\n");
            exit(err);
        }
    }


#ifdef X86MODULES
    if (encl->use_x86_acc)
        load_x86_kernel_modules();
#endif

    // Now that our kernel is ready to handle syscalls, mount root
    lkl_mount_virtual();
    SGXLKL_VERBOSE("essential mount points mounted\n");
    init_random();
    SGXLKL_VERBOSE("random init complete\n");

    // Set environment variable to export SHMEM address to the application.
    // Note: Due to how putenv() works, we need to allocate the environment
    // variable on the heap and we must _not_ free it (man putenv, section NOTES)
    char *shm_common = malloc(64);
    char *shm_enc_to_out_addr = malloc(64);
    char *shm_out_to_enc_addr = malloc(64);

    // Set address of ring buffer to env, so that enclave process can access it directly
    snprintf(shm_common, 64, "SGXLKL_SHMEM_COMMON=%p", encl->shm_common);
    snprintf(shm_enc_to_out_addr, 64, "SGXLKL_SHMEM_ENC_TO_OUT=%p", encl->shm_enc_to_out);
    snprintf(shm_out_to_enc_addr, 64, "SGXLKL_SHMEM_OUT_TO_ENC=%p", encl->shm_out_to_enc);
    putenv(shm_common);
    putenv(shm_enc_to_out_addr);
    putenv(shm_out_to_enc_addr);

    // Sysctl
    do_sysctl(encl);
    SGXLKL_VERBOSE("sysctl completed\n");

    // Set interface status/IP/routes
    if (!sgxlkl_use_host_network && net_dev_id != -1) {
        SGXLKL_VERBOSE("doing lkl_poststart_net\n");
        lkl_poststart_net(encl, net_dev_id);
    }

    SGXLKL_VERBOSE("doing spdk initialization\n");
    int rc = sgxlkl_spdk_initialize();
    if (rc < 0) {
        fprintf(stderr, "Error: unable to initialize spdk, %s\n",
                strerror(-rc));
        exit(rc);
    }
    // Set interface status/IP/routes
    if (!sgxlkl_use_host_network) {
        SGXLKL_VERBOSE("setting dpdk configuration...\n");
        lkl_prestart_dpdk(encl);
        SGXLKL_VERBOSE("prestart dpdk done\n");
        lkl_poststart_dpdk(encl);
        SGXLKL_VERBOSE("poststart dpdk done\n");
    }

    spdk_context = encl->spdk_context;

    // Set up wireguard
    //init_wireguard(encl);

    // Set hostname (provided through SGXLKL_HOSTNAME)
    sethostname(encl->hostname, strlen(encl->hostname));
    SGXLKL_VERBOSE("lkl_start_init finished\n");
}

/* Requires starttime to be higher or equal to endtime */
int timespec_diff(struct timespec *starttime, struct timespec *endtime, struct timespec *diff) {
    if (starttime->tv_sec > endtime->tv_sec || (starttime->tv_sec == endtime->tv_sec && starttime->tv_nsec > endtime->tv_nsec)) {
        errno = EINVAL;
        return -1;
    }

    diff->tv_sec = endtime->tv_sec - starttime->tv_sec;
    if (starttime->tv_nsec > endtime->tv_nsec) {
        diff->tv_sec--;
    }

    diff->tv_nsec = (1000000000 + endtime->tv_nsec - starttime->tv_nsec) % 1000000000;

    return 0;
}

void lkl_exit() {
    if (getenv("SGXLKL_PRINT_APP_RUNTIME")) {
        struct timespec endtime, runtime;
        clock_gettime(CLOCK_MONOTONIC, &endtime);
        timespec_diff(&sgxlkl_app_starttime, &endtime, &runtime);
        printf("Application runtime: %lld.%.9lds\n", runtime.tv_sec, runtime.tv_nsec);
    }

    // Stop attestation/remote control server
    enclave_cmd_servers_stop();

    // Switch back to root so we can unmount all filesystems
    lkl_set_working_dir("/");

    SGXLKL_VERBOSE("Stop spdk\n");
    // too slow in some benchmarks,
    // since we throw the disk image away afterwards anyway,
    // we just skip this step
    if (!spdk_context->skip_unmount) {
        lkl_stop_spdk();
    }
    SGXLKL_VERBOSE("Stop dpdk\n");
    lkl_stop_dpdk();

    SGXLKL_VERBOSE("Unmount disks\n");
    // Unmount disks
    long res;
    for (int i = num_disks - 1; i >= 0; --i) {
        if (!disks[i].mounted)
            continue;

        res = lkl_umount_timeout(disks[i].mnt, 0, UMOUNT_DISK_TIMEOUT);
        if (res < 0) {
            fprintf(stderr, "Error: Could not unmount disk %d, %s\n", i, lkl_strerror(res));
        }

        // Root disk, no need to remove mount point ("/").
        if (i == 0) break;


        // Not really necessary for mounts in /mnt since /mnt is
        // mounted as tmpfs itself, but it is also possible to mount
        // secondary images at any place in the root file system,
        // including persistent storage, if the root file system is
        // writeable. For simplicity, remove all mount points here.
        //
        // Note: We currently do not support pre-existing mount points
        // on read-only file systems.
        res = lkl_sys_rmdir(disks[i].mnt);
        if (res < 0) {
            fprintf(stderr, "Error: Could not remove mount point %s\n", disks[i].mnt, lkl_strerror(res));
        }
    }

    spdk_context_detach(spdk_context);

    res = lkl_sys_halt();
    if (res < 0) {
        fprintf(stderr, "Error: LKL halt, %s\n",
            lkl_strerror(res));
        exit(res);
    }
}
