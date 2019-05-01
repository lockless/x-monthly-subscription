#include <linux/bpf.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

struct data {
    __u64 timestamp;
    __u64 data[3];
};

struct raw_data {
    __u64 timestamp;
    __u64 data[3];
};

struct pkt_meta {
	union {
		__be32 src;
		__be32 srcv6[4];
	};
	union {
		__be32 dst;
		__be32 dstv6[4];
	};
	__u16 port16[2];
	__u16 l3_proto;
	__u16 l4_proto;
	__u16 data_len;
	__u16 pkt_len;
	__u32 seq;
};

struct bpf_map_def SEC("maps") window_map = {
    .type = BPF_MAP_TYPE_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(struct data),
    .max_entries = 512,
};

struct bpf_map_def SEC("maps") array_index = {
    .type = BPF_MAP_TYPE_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = 1,
};

/* parser from official xdp example */
/* assuming: ipv4 & udp */
static __always_inline bool parse_udp(void *data, __u64 off, void *data_end,
				      struct pkt_meta *pkt)
{
	struct udphdr *udp;

	udp = data + off;
	if (udp + 1 > data_end)
		return false;

	pkt->port16[0] = udp->source;
	pkt->port16[1] = udp->dest;
	return true;
}

static __always_inline bool parse_ip4(void *data, __u64 off, void *data_end,
				      struct pkt_meta *pkt)
{
	struct iphdr *iph;

	iph = data + off;
	if (iph + 1 > data_end)
		return false;

	if (iph->ihl != 5)
		return false;

	pkt->src = iph->saddr;
	pkt->dst = iph->daddr;
	pkt->l4_proto = iph->protocol;

	return true;
}

static __always_inline bool parse_fjw(void *data, __u64 off, void *data_end,
                        struct pkt_meta *pkt)
{
    return true;
}

int process_packet(struct xdp_md *ctx)
{
    // ctx->rx_queue_index = 1;  // if 切割, XDP_PASS; else XDP_DROP
    void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;
    struct pkt_meta pkt = {};
    __u32 off;

    off = sizeof(struct ethhdr);
    if (data + off > data_end) {
        return XDP_DROP;  // not a valid ethernet packet
    }

    pkt.l3_proto = bpf_htons(eth->h_proto);  // get next layer protocol

    if (pkt.l3_proto == ETH_P_IP) {
        // ipv4
        if (!parse_ip4(data, off, data_end, &pkt)) {
            return XDP_DROP;  // not a valid ipv4 packet
        }
        off += sizeof(struct iphdr);
    } else {
        return XDP_DROP;  // won't handle ipv6
    }

    if (data + off > data_end) {
        return XDP_DROP;
    }

    if (pkt.l4_proto == IPPROTO_UDP) {
        if (!parse_udp(data, off, data_end, &pkt)) {
            return XDP_DROP;
        }
        off += sizeof(struct udphdr);
    } else {
        return XDP_DROP;
    }

    pkt.pkt_len = data_end - data;
	pkt.data_len = data_end - data - off;

    return XDP_PASS;
}

char _license[] SEC("license") = "FJW";