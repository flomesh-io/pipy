#include <stdlib.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include "bpf-builtin.h"
#include "bpf-utils.h"

#define TRACING 0
#define MAX_ROUTES 100
#define MAX_BALANCERS 1024
#define MAX_UPSTREAMS 65536
#define MAX_CONNECTIONS 65536
#define RING_SIZE 16

char __license[] SEC("license") = "Dual MIT/GPL";

//
// BPF map structures
//

struct IP {
  __u32 v4;
};

struct IPMask {
  struct bpf_lpm_trie_key mask;
  struct IP ip;
};

struct Address {
  struct IP ip;
  __u16 port;
};

struct Endpoint {
  struct Address addr;
  __u8 proto;
};

struct Route {
  __u32 interface;
  __u8 broadcast[ETH_ALEN];
};

struct Balancer {
  __u32 ring[RING_SIZE];
  __u32 hint;
};

struct Upstream {
  struct Address addr;
};

struct NATKey {
  struct Address src;
  struct Address dst;
  __u8 proto;
};

struct NATVal {
  struct Address src;
  struct Address dst;
  __u32 interface;
  __u8 mac[ETH_ALEN];
  __u8 is_return;
};

//
// BPF map definitions
//

struct {
  int (*type)[BPF_MAP_TYPE_LPM_TRIE];
  int (*max_entries)[MAX_ROUTES];
  int (*map_flags)[BPF_F_NO_PREALLOC];
  struct IPMask *key;
  struct Route *value;
} map_routes SEC(".maps");

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[MAX_BALANCERS];
  struct Endpoint *key;
  struct Balancer *value;
} map_balancers SEC(".maps");

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[MAX_UPSTREAMS];
  __u32 *key;
  struct Upstream *value;
} map_upstreams SEC(".maps");

struct {
  int (*type)[BPF_MAP_TYPE_LRU_HASH];
  int (*max_entries)[MAX_CONNECTIONS];
  struct NATKey *key;
  struct NATVal *value;
} map_nat SEC(".maps");

struct EthInfo {
  struct ethhdr *hdr;
  __u16 proto;
  __u8 src[ETH_ALEN];
  __u8 dst[ETH_ALEN];
};

struct IPInfo {
  struct iphdr *hdr;
  __u8 proto;
  struct IP src, dst;
};

struct TCPInfo {
  struct tcphdr *hdr;
  __u16 src, dst;
};

struct UDPInfo {
  struct udphdr *hdr;
  __u16 src, dst;
};

//
// Packet - All runtime data related to the processing of a packet
//

struct Packet {
  void *ptr;
  void *end;

  struct EthInfo eth;
  struct IPInfo ip;
  struct TCPInfo tcp;
  struct UDPInfo udp;
};

INLINE int parse_eth(struct Packet *pkt) {
  struct ethhdr *eth = pkt->eth.hdr = pkt->ptr;
  if (eth + 1 > pkt->end) return 0;
  pkt->ptr += sizeof(*eth);

  pkt->eth.proto = ntohs(eth->h_proto);
  __builtin_memcpy(pkt->eth.src, eth->h_source, ETH_ALEN);
  __builtin_memcpy(pkt->eth.dst, eth->h_dest, ETH_ALEN);

  return 1;
}

INLINE int parse_ipv4(struct Packet *pkt) {
  struct iphdr *ip = pkt->ip.hdr = pkt->ptr;
  if (pkt->end < ip + 1) return 0;
  if (pkt->end < (pkt->ptr += ip->ihl << 2)) return 0;

  pkt->ip.proto = ip->protocol;
  pkt->ip.src.v4 = ip->saddr;
  pkt->ip.dst.v4 = ip->daddr;

  return 1;
}

INLINE int parse_tcp(struct Packet *pkt) {
  struct tcphdr *tcp = pkt->tcp.hdr = pkt->ptr;
  if (pkt->end < tcp + 1) return 0;
  if (pkt->end < (pkt->ptr += tcp->doff)) return 0;

  pkt->tcp.src = ntohs(tcp->source);
  pkt->tcp.dst = ntohs(tcp->dest);

  return 1;
}

INLINE int parse_udp(struct Packet *pkt) {
  struct udphdr *udp = pkt->udp.hdr = pkt->ptr;
  if (pkt->end < udp + 1) return 0;
  pkt->ptr += sizeof(*udp);

  pkt->udp.src = ntohs(udp->source);
  pkt->udp.dst = ntohs(udp->dest);

  return 1;
}

INLINE __u16 fold_csum(__u32 csum) {
  return (csum & 0xffff) + (csum >> 16);
}

INLINE void alter_eth_src(struct Packet *pkt, __u8 mac[ETH_ALEN]) {
  __builtin_memcpy(pkt->eth.hdr->h_source, mac, ETH_ALEN);
}

INLINE void alter_eth_dst(struct Packet *pkt, __u8 mac[ETH_ALEN]) {
  __builtin_memcpy(pkt->eth.hdr->h_dest, mac, ETH_ALEN);
}

INLINE void alter_ip(struct Packet *pkt, void *ptr, struct IP *ip) {
  struct iphdr *h = pkt->ip.hdr;
  __u32 *p = (void *)ptr;
  __u32 *q = (void *)&ip->v4;
  __u32 csum = bpf_csum_diff(p, sizeof(*p), q, sizeof(*q), 0xffff - h->check);
  h->check = 0xffff - fold_csum(csum);
  switch (pkt->ip.proto) {
    case IPPROTO_TCP:
      csum = bpf_csum_diff(p, sizeof(*p), q, sizeof(*q), 0xffff - pkt->tcp.hdr->check);
      pkt->tcp.hdr->check = 0xffff - fold_csum(csum);
      break;
    case IPPROTO_UDP:
      csum = bpf_csum_diff(p, sizeof(*p), q, sizeof(*q), 0xffff - pkt->udp.hdr->check);
      pkt->udp.hdr->check = 0xffff - fold_csum(csum);
      break;
  }
  __builtin_memcpy(p, q, sizeof(*p));
}

INLINE void alter_ports(struct Packet *pkt, void *from, void *to) {
  __u32 *p = (void *)from;
  __u32 *q = (void *)to;
  __u32 csum;
  switch (pkt->ip.proto) {
    case IPPROTO_TCP:
      csum = bpf_csum_diff(p, sizeof(*p), q, sizeof(*q), 0xffff - pkt->tcp.hdr->check);
      pkt->tcp.hdr->check = 0xffff - fold_csum(csum);
      break;
    case IPPROTO_UDP:
      csum = bpf_csum_diff(p, sizeof(*p), q, sizeof(*q), 0xffff - pkt->udp.hdr->check);
      pkt->udp.hdr->check = 0xffff - fold_csum(csum);
      break;
  }
  __builtin_memcpy(p, q, sizeof(*p));
}

INLINE void alter_ip_src(struct Packet *pkt, struct IP *ip) {
  alter_ip(pkt, &pkt->ip.hdr->saddr, ip);
}

INLINE void alter_ip_dst(struct Packet *pkt, struct IP *ip) {
  alter_ip(pkt, &pkt->ip.hdr->daddr, ip);
}

INLINE void alter_tcp_src(struct Packet *pkt, __u16 port) {
  struct tcphdr *h = pkt->tcp.hdr;
  __u16 buf[2] = { htons(port), h->dest };
  alter_ports(pkt, &h->source, buf);
}

INLINE void alter_tcp_dst(struct Packet *pkt, __u16 port) {
  struct tcphdr *h = pkt->tcp.hdr;
  __u16 buf[2] = { h->source, htons(port) };
  alter_ports(pkt, &h->source, buf);
}

INLINE void alter_udp_src(struct Packet *pkt, __u16 port) {
  struct udphdr *h = pkt->udp.hdr;
  __u16 buf[2] = { htons(port), h->dest };
  alter_ports(pkt, &h->source, buf);
}

INLINE void alter_udp_dst(struct Packet *pkt, __u16 port) {
  struct udphdr *h = pkt->udp.hdr;
  __u16 buf[2] = { h->source, htons(port) };
  alter_ports(pkt, &h->source, buf);
}

INLINE void alter_l4_src(struct Packet *pkt, struct Address *addr) {
  alter_ip_src(pkt, &addr->ip);
  switch (pkt->ip.proto) {
    case IPPROTO_TCP: alter_tcp_src(pkt, addr->port); break;
    case IPPROTO_UDP: alter_udp_src(pkt, addr->port); break;
  }
}

INLINE void alter_l4_dst(struct Packet *pkt, struct Address *addr) {
  alter_ip_dst(pkt, &addr->ip);
  switch (pkt->ip.proto) {
    case IPPROTO_TCP: alter_tcp_dst(pkt, addr->port); break;
    case IPPROTO_UDP: alter_udp_dst(pkt, addr->port); break;
  }
}

INLINE void trace_packet(struct Packet *pkt, const char *msg) {
  debug_printf("%s", msg);
  __u16 *s = (void *)pkt->eth.hdr->h_source;
  __u16 *d = (void *)pkt->eth.hdr->h_dest;
  debug_printf("  eth");
  debug_printf("    src %04x %04x %04x", ntohs(s[0]), ntohs(s[1]), ntohs(s[2]));
  debug_printf("    dst %04x %04x %04x", ntohs(d[0]), ntohs(d[1]), ntohs(d[2]));
  debug_printf("  ip");
  debug_printf("    len %d", pkt->end - (void *)pkt->ip.hdr);
  switch (pkt->ip.proto) {
    case IPPROTO_TCP:
      debug_printf("    seq %u", ntohl(pkt->tcp.hdr->seq));
      debug_printf("    ack %u", ntohl(pkt->tcp.hdr->ack_seq));
      debug_printf("    src %08x %d", ntohl(pkt->ip.hdr->saddr), ntohs(pkt->tcp.hdr->source));
      debug_printf("    dst %08x %d", ntohl(pkt->ip.hdr->daddr), ntohs(pkt->tcp.hdr->dest));
      debug_printf("    flags %02x", *((__u8 *)pkt->tcp.hdr + 13));
      break;
    case IPPROTO_UDP:
      debug_printf("    src %08x %d", ntohl(pkt->ip.hdr->saddr), ntohs(pkt->udp.hdr->source));
      debug_printf("    dst %08x %d", ntohl(pkt->ip.hdr->daddr), ntohs(pkt->udp.hdr->dest));
      break;
  }
}

#if TRACING
#define TRACE(msg) do         \
  {                           \
    char _msg[] = msg;        \
    trace_packet(&pkt, _msg); \
  } while (0)
#else
#define TRACE(msg) do {} while (0)
#endif // TRACING

//
// XDP packet entrance point
//

SEC("xdp")
int xdp_main(struct xdp_md *ctx) {
  struct Packet pkt;
  pkt.ptr = (void *)(long)ctx->data;
  pkt.end = (void *)(long)ctx->data_end;

  struct Address src, dst;

  if (!parse_eth(&pkt)) return XDP_PASS;

  switch (pkt.eth.proto) {
    case ETH_P_IP:
      if (!parse_ipv4(&pkt)) return XDP_PASS;
      src.ip = pkt.ip.src;
      dst.ip = pkt.ip.dst;

      switch (pkt.ip.proto) {
        case IPPROTO_TCP:
          if (!parse_tcp(&pkt)) return XDP_PASS;
          src.port = pkt.tcp.src;
          dst.port = pkt.tcp.dst;
          break;
        case IPPROTO_UDP:
          if (!parse_udp(&pkt)) return XDP_PASS;
          src.port = pkt.udp.src;
          dst.port = pkt.udp.dst;
          break;
        default: return XDP_PASS;
      }
      break;

    default: return XDP_PASS;
  }

  struct NATKey nat_key;
  struct NATVal *nat;

  __builtin_memset(&nat_key, 0, sizeof(nat_key));

  nat_key.proto = pkt.ip.proto;
  nat_key.src = src;
  nat_key.dst = dst;
  nat = bpf_map_lookup_elem(&map_nat, &nat_key);

  if (nat) {
    if (nat->is_return) {
      alter_eth_src(&pkt, pkt.eth.dst);
      alter_eth_dst(&pkt, nat->mac);
      alter_l4_src(&pkt, &nat->src);
      alter_l4_dst(&pkt, &nat->dst);
      TRACE("translate return");
    } else {
      alter_eth_src(&pkt, pkt.eth.dst);
      alter_eth_dst(&pkt, nat->mac);
      alter_l4_src(&pkt, &nat->src);
      alter_l4_dst(&pkt, &nat->dst);
      TRACE("translate forward");
    }
    return XDP_TX;
  }

  struct Endpoint ep;
  __builtin_memset(&ep, 0, sizeof(ep));
  ep.addr = dst;
  ep.proto = pkt.ip.proto;

  struct Balancer *balancer = bpf_map_lookup_elem(&map_balancers, &ep);
  if (balancer) {
    __u32 sel = balancer->hint % RING_SIZE;
    struct Upstream *upstream = bpf_map_lookup_elem(&map_upstreams, &balancer->ring[sel]);
    if (upstream) {
      struct Address fwd_src, fwd_dst;
      fwd_src = dst;
      fwd_dst = upstream->addr;

      struct IPMask rt_key;
      rt_key.mask.prefixlen = 32;
      rt_key.ip = fwd_dst.ip;

      struct Route *route = bpf_map_lookup_elem(&map_routes, &rt_key);
      if (!route) return XDP_DROP;

      struct NATKey nat_key;
      struct NATVal nat_val;
      __builtin_memset(&nat_key, 0, sizeof(nat_key));
      __builtin_memset(&nat_val, 0, sizeof(nat_val));

      nat_key.proto = pkt.ip.proto;
      nat_key.src = src;
      nat_key.dst = dst;
      nat_val.src = fwd_src;
      nat_val.dst = fwd_dst;
      nat_val.interface = route->interface;
      __builtin_memcpy(&nat_val.mac, route->broadcast, sizeof(nat_val.mac));
      bpf_map_update_elem(&map_nat, &nat_key, &nat_val, BPF_ANY);

      nat_key.src = fwd_dst;
      nat_key.dst = fwd_src;
      nat_val.src = dst;
      nat_val.dst = src;
      nat_val.interface = ctx->ingress_ifindex;
      nat_val.is_return = 1;
      __builtin_memcpy(&nat_val.mac, &pkt.eth.src, sizeof(nat_val.mac));
      bpf_map_update_elem(&map_nat, &nat_key, &nat_val, BPF_ANY);

      alter_eth_src(&pkt, pkt.eth.dst);
      alter_eth_dst(&pkt, route->broadcast);
      alter_l4_src(&pkt, &nat_key.dst);
      alter_l4_dst(&pkt, &upstream->addr);

      TRACE("start tracking");
      return XDP_TX;
    }
  }

  return XDP_PASS;
}
