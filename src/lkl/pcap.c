#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/uio.h>
#include <unistd.h>

#include "sgx_hostcalls.h"

typedef struct __attribute__((packed)) pcap_hdr_s {
    uint32_t magic_number;  /* magic number */
    uint16_t version_major; /* major version number */
    uint16_t version_minor; /* minor version number */
    int32_t thiszone;       /* GMT to local correction */
    uint32_t sigfigs;       /* accuracy of timestamps */
    uint32_t snaplen;       /* max length of captured packets, in octets */
    uint32_t network;       /* data link type */
} pcap_hdr_t;

typedef struct __attribute__((packed)) pcaprec_hdr_s {
    uint32_t ts_sec;   /* timestamp seconds */
    uint32_t ts_usec;  /* timestamp microseconds */
    uint32_t incl_len; /* number of octets of packet saved in file */
    uint32_t orig_len; /* actual length of packet */
} pcaprec_hdr_t;

#ifdef DEBUG
int write_pcap_file(const char* filename, void* pkt, size_t len) {
    int fd = host_syscall_SYS_open(filename, O_WRONLY | O_CREAT, 0);
    if (fd < 0) {
        return -errno;
    }

    pcap_hdr_t pcap_hdr = {.magic_number = 0xa1b2c3d4,
                           .version_major = 2,
                           .version_minor = 4,
                           .thiszone = 0,
                           .sigfigs = 0,
                           .snaplen = len,
                           .network = 1};

    pcaprec_hdr_t pcaprec_hdr = {
        .ts_sec = 0, .ts_usec = 0, .incl_len = len, .orig_len = len};
    struct iovec iov[3] = {
        {
            .iov_base = &pcap_hdr,
            .iov_len = sizeof(pcap_hdr_t),
        },
        {
            .iov_base = &pcaprec_hdr,
            .iov_len = sizeof(pcaprec_hdr_t),
        },
        {
            .iov_base = pkt,
            .iov_len = len,
        },
    };

    if (host_syscall_SYS_writev(fd, iov, 3) < 0) {
        host_syscall_SYS_close(fd);
        return -errno;
    };
    return host_syscall_SYS_close(fd);
}
#endif
