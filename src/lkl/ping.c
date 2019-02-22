#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <pthread.h>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>

#include "ping.h"

struct eth_hdr {
    uint8_t dmac[6];
    uint8_t smac[6];
    uint16_t ethertype;
} __attribute__((packed));

struct ip_hdr {
    uint8_t ip_hl:4,
            ip_v:4;
    uint8_t ip_tos;
    uint16_t ip_len;
    uint16_t ip_id;
    uint16_t ip_off;
#define  IP_DF 0x4000
#define  IP_MF 0x2000
    uint8_t ip_ttl;
    uint8_t ip_p;
    uint16_t ip_sum;
    struct in_addr ip_src,ip_dst;	/* source and dest address */
} __attribute__((packed));

struct icmphdr {
    uint8_t type;		/* message type */
    uint8_t code;		/* type sub-code */
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;           /* echo datagram */
        uint32_t gateway; /* gateway address */
        struct {
            uint16_t __unused;
            uint16_t mtu;
        } frag; /* path mtu discovery */
    } un;
} __attribute__((packed));

static uint16_t ip_checksum(void* vdata,size_t length) {
    // Cast the data pointer to one that can be indexed.
    char* data=(char*)vdata;

    // Initialise the accumulator.
    uint32_t acc=0xffff;

    // Handle complete 16-bit blocks.
    for (size_t i=0;i+1<length;i+=2) {
        uint16_t word;
        memcpy(&word,data+i,2);
        acc+=ntohs(word);
        if (acc>0xffff) {
            acc-=0xffff;
        }
    }

    // Handle any partial block at the end of the data.
    if (length&1) {
        uint16_t word=0;
        memcpy(&word,data+length-1,1);
        acc+=ntohs(word);
        if (acc>0xffff) {
            acc-=0xffff;
        }
    }

    // Return the checksum in network byte order.
    return htons(~acc);
}

#define IPV4_ADDR(a, b, c, d)(((a & 0xff) << 24) | ((b & 0xff) << 16) | \
                              ((c & 0xff) << 8) | (d & 0xff))

void ping_packet(void* pkt) {
    struct eth_hdr ether = {
        .dmac = { 0x3c, 0xfd, 0xfe, 0xa2, 0x30, 0xc8 },
        .smac = { 0x3c, 0xfd, 0xfe, 0xa2, 0x2d, 0x78 },
        .ethertype = htons(0x0800),
    };

    struct ip_hdr ip = {
		.ip_hl = 5,
        .ip_v = 4, // IPv4
        .ip_tos = 0,
        .ip_len = htons(sizeof(struct ip_hdr) + sizeof(struct icmphdr)),
        .ip_id = htons(0),
        .ip_off = htons(0),
        .ip_ttl = 2,
        .ip_p = 1, // ICMP
        .ip_sum = 0,
        .ip_src = htonl(IPV4_ADDR(10, 0, 2, 1)),
        .ip_dst = htonl(IPV4_ADDR(10, 0, 2, 2)),
    };

    size_t ip_sum_len =
        sizeof(uint8_t) + // ip_hl + ip_v
        sizeof(uint8_t) + // ip_tos
        sizeof(short) +   // ip_len
        sizeof(short) +   // ip_id
        sizeof(short) +   // ip_off
        sizeof(uint8_t) + // ip_ttl
        sizeof(uint8_t);  // ip_p

    ip.ip_sum = ip_checksum(&ip, ip_sum_len);

    struct icmphdr icmp = {
        .type = 8, // ECHO
        .code = 0,
        .checksum = 0,
        .un = {.echo = {.id = htons(1), .sequence = htons(1)}},
    };

    icmp.checksum = ip_checksum(&icmp, sizeof(icmp));

    memcpy(pkt, &ether, sizeof(ether));
    memcpy((char*)pkt + sizeof(struct eth_hdr), &ip, sizeof(struct ip_hdr));
    memcpy((char*)pkt + sizeof(struct eth_hdr) + sizeof(struct ip_hdr), &icmp, sizeof(struct ip_hdr));
}

int send_ping(int portid, struct rte_mempool* txpool) {
    struct rte_mbuf *rm = rte_pktmbuf_alloc(txpool);
    if (!rm) {
        fprintf(stderr, "dpdk: failed to allocate packet\n");
        return -ENOMEM;
    }

    size_t len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + sizeof(struct icmphdr);
    void *pkt = rte_pktmbuf_append(rm, len);
    if (!pkt) {
        fprintf(stderr, "dpdk: append icmp packet to buffer\n");
        rte_pktmbuf_free(rm);
        return -ENOMEM;
    }

    ping_packet(pkt);

    rte_eth_tx_burst(portid, 0, &rm, 1);

    rte_pktmbuf_free(rm);

    return 0;
}
