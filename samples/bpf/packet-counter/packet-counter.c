#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include "bpf-builtin.h"
#include "bpf-utils.h"

char __license[] SEC("license") = "Dual MIT/GPL";

#define MAX_ENTRIES 16

struct {
  int (*type)[BPF_MAP_TYPE_LRU_HASH];
  int (*max_entries)[MAX_ENTRIES];
  __u32 *key;
  __u32 *value;
} packet_counts SEC(".maps");

SEC("xdp")
int xdp_prog_func(struct xdp_md *ctx) {
  void *pkt = (void *)(long)ctx->data;
  void *end = (void *)(long)ctx->data_end;

  struct ethhdr *eth = pkt;
  if (eth + 1 > end) return XDP_PASS;
  if (ntohs(eth->h_proto) != ETH_P_IP) return XDP_PASS;

  struct iphdr *ip = (void *)(eth + 1);
  if (ip + 1 > end) return XDP_PASS;

  __u32 addr = ip->saddr;
  __u32 *cnt = bpf_map_lookup_elem(&packet_counts, &addr);

  if (!cnt) {
    __u32 n = 1;
    bpf_map_update_elem(&packet_counts, &addr, &n, BPF_ANY);
  } else {
    __sync_fetch_and_add(cnt, 1);
  }

  return XDP_PASS;
}
