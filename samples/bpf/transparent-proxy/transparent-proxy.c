#include <stddef.h>
#include <linux/bpf.h>
#include <linux/netfilter_ipv4.h>
#include <linux/in.h>

#include "bpf-builtin.h"
#include "bpf-utils.h"

#define MAX_CONNECTIONS 20000

struct Config {
  __u16 proxy_port;
  __u64 pipy_pid;
};

struct Socket {
  __u32 src_addr;
  __u16 src_port;
  __u32 dst_addr;
  __u16 dst_port;
};

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[1];
  __u32 *key;
  struct Config *value;
} map_config SEC(".maps");

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[MAX_CONNECTIONS];
  __u64 *key;
  struct Socket *value;
} map_socks SEC(".maps");

struct {
  int (*type)[BPF_MAP_TYPE_HASH];
  int (*max_entries)[MAX_CONNECTIONS];
  __u16 *key;
  __u64 *value;
} map_ports SEC(".maps");

int cg_connect4(struct bpf_sock_addr *ctx) {
  if (ctx->user_family != 2) return 1;
  if (ctx->protocol != IPPROTO_TCP) return 1;

  __u32 i = 0;
  struct Config *conf = bpf_map_lookup_elem(&map_config, &i);
  if (!conf) return 1;
  if ((bpf_get_current_pid_tgid() >> 32) == conf->pipy_pid) return 1;

  __u32 dst_addr = ntohl(ctx->user_ip4);
  __u16 dst_port = ntohl(ctx->user_port) >> 16;
  __u64 cookie = bpf_get_socket_cookie(ctx);

  struct Socket sock;
  __builtin_memset(&sock, 0, sizeof(sock));
  sock.dst_addr = dst_addr;
  sock.dst_port = dst_port;
  bpf_map_update_elem(&map_socks, &cookie, &sock, 0);

  ctx->user_ip4 = htonl(0x7f000001);
  ctx->user_port = htonl(conf->proxy_port << 16);

  return 1;
}

int cg_sock_ops(struct bpf_sock_ops *ctx) {
  if (ctx->family != 2) return 0;

  if (ctx->op == BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB) {
    __u64 cookie = bpf_get_socket_cookie(ctx);
    struct Socket *sock = bpf_map_lookup_elem(&map_socks, &cookie);
    if (sock) {
      __u32 src_addr = ntohl(ctx->local_ip4);
      __u16 src_port = ctx->local_port;
      sock->src_addr = src_addr;
      sock->src_port = src_port;
      bpf_map_update_elem(&map_ports, &src_port, &cookie, 0);
    }
  }

  return 0;
}

int cg_sock_opt(struct bpf_sockopt *ctx) {
  if (ctx->optname != SO_ORIGINAL_DST) return 1;
  if (ctx->sk->family != 2) return 1;
  if (ctx->sk->protocol != IPPROTO_TCP) return 1;

  __u16 src_port = ntohs(ctx->sk->dst_port);
  __u64 *cookie = bpf_map_lookup_elem(&map_ports, &src_port);
  if (!cookie) return 1;

  struct Socket *sock = bpf_map_lookup_elem(&map_socks, cookie);
  if (!sock) return 1;

  struct sockaddr_in *sa = ctx->optval;
  if (sa + 1 > ctx->optval_end) return 1;

  ctx->optlen = sizeof(*sa);
  sa->sin_family = ctx->sk->family;
  sa->sin_addr.s_addr = htonl(sock->dst_addr);
  sa->sin_port = htons(sock->dst_port);

  ctx->retval = 0;
  return 1;
}

char __LICENSE[] SEC("license") = "GPL";
