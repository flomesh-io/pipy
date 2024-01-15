#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include "bpf-builtin.h"
#include "bpf-utils.h"

struct Key {
  union {
    __u8 u8[4];
    __u32 u32;
  } ip;
};

struct {
  int (*type)[BPF_MAP_TYPE_LRU_HASH];
  int (*max_entries)[1000];
  struct Key *key;
  __u32 *value;
} map_pkt_cnt SEC(".maps");

int xdp_main(struct xdp_md *ctx) {
  void *pkt = (void *)(long)ctx->data;
  void *end = (void *)(long)ctx->data_end;

  struct ethhdr *eth = pkt;
  if (eth + 1 > end) return XDP_PASS;
  if (ntohs(eth->h_proto) != ETH_P_IP) return XDP_PASS;

  struct iphdr *ip = (void *)(eth + 1);
  if (ip + 1 > end) return XDP_PASS;

  struct Key k;
  k.ip.u32 = ip->saddr;

  __u32 *cnt = bpf_map_lookup_elem(&map_pkt_cnt, &k);

  if (!cnt) {
    __u32 n = 1;
    bpf_map_update_elem(&map_pkt_cnt, &k, &n, BPF_ANY);
  } else {
    __sync_fetch_and_add(cnt, 1);
  }

  return XDP_PASS;
}
