#define SEC(name) __attribute__((section(name), used))
#define INLINE __attribute__((__always_inline__)) static inline

#define debug_printf(fmt, ...) do                              \
  {                                                            \
    char _fmt[] = fmt;                                      \
    bpf_trace_printk(_fmt, sizeof(_fmt), ##__VA_ARGS__); \
  } while (0)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define ntohs(x) __builtin_bswap16(x)
#define htons(x) __builtin_bswap16(x)
#define ntohl(x) __builtin_bswap32(x)
#define htonl(x) __builtin_bswap32(x)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ntohs(x) (x)
#define htons(x) (x)
#define ntohl(x) (x)
#define htonl(x) (x)
#else
#error "Unknown __BYTE_ORDER__"
#endif
