#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/pkt_cls.h>

#include "bpf-builtin.h"
#include "bpf-utils.h"

#define MAX_PORTS 100

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[MAX_PORTS];
  __u16 *key;
  __u16 *value;
} map_dnat SEC(".maps");

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[MAX_PORTS];
  __u16 *key;
  __u16 *value;
} map_snat SEC(".maps");

int tc_main(struct __sk_buff *skb) {
  void *pkt = (void *)(long)skb->data;
  void *end = (void *)(long)skb->data_end;

  struct ethhdr *eth = pkt;
  struct iphdr *ip = (void *)(eth + 1);
  struct tcphdr *tcp = (void *)(ip + 1);
  if (tcp + 1 > end) return TC_ACT_OK;

  if (
    ntohs(eth->h_proto) != ETH_P_IP ||
    ip->protocol != IPPROTO_TCP
  ) return TC_ACT_OK;

  // Ingress
  if (skb->ingress_ifindex) {
    __u16 ori_port = ntohs(tcp->dest);
    __u16 *p = bpf_map_lookup_elem(&map_dnat, &ori_port);
    if (p) {
      __u16 new_port = htons(*p);
      bpf_skb_store_bytes(skb, (void *)&tcp->dest - pkt, &new_port, sizeof(new_port), BPF_F_RECOMPUTE_CSUM);
    }

  // Egress
  } else {
    __u16 ori_port = ntohs(tcp->source);
    __u16 *p = bpf_map_lookup_elem(&map_snat, &ori_port);
    if (p) {
      __u16 new_port = htons(*p);
       bpf_skb_store_bytes(skb, (void *)&tcp->source - pkt, &new_port, sizeof(new_port), BPF_F_RECOMPUTE_CSUM);
    }
  }

  return TC_ACT_OK;
}
