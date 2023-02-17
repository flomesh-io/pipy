/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "netmask.hpp"
#include "utils.hpp"

namespace pipy {

static inline auto get_ip4(const uint8_t ip[4]) -> uint32_t {
  return (
    ((uint32_t)ip[0] << 24) |
    ((uint32_t)ip[1] << 16) |
    ((uint32_t)ip[2] <<  8) |
    ((uint32_t)ip[3] <<  0)
  );
}

static inline auto ip4_to_str(const uint8_t ip[4]) -> pjs::Str* {
  char str[100];
  std::sprintf(
    str, "%d.%d.%d.%d",
    (int)ip[0],
    (int)ip[1],
    (int)ip[2],
    (int)ip[3]
  );
  return pjs::Str::make(str);
}

static inline auto ip4_to_str(uint32_t ip) -> pjs::Str* {
  uint8_t n[4];
  n[0] = ip >> 24;
  n[1] = ip >> 16;
  n[2] = ip >> 8;
  n[3] = ip >> 0;
  return ip4_to_str(n);
}

static inline auto ip6_to_str(const uint16_t ip[8]) -> pjs::Str* {
  int zero_i = 0;
  int zero_n = 0;
  for (int i = 0, n = 0; i < 8; i++) {
    if (ip[i]) {
      n = 0;
    } else {
      if (++n > zero_n) {
        zero_n = n;
        zero_i = i;
      }
    }
  }

  char str[50];
  int p = 0;

  auto write = [&](uint16_t n) {
    static char hex[] = { "0123456789abcdef" };
    if (p > 0) str[p++] = ':';
    bool nz = false;
    for (int i = 12; i >= 0; i -= 4) {
      auto h = (n >> i) & 15;
      if (nz || h) {
        str[p++] = hex[h];
        nz = true;
      }
    }
  };

  if (zero_n > 1) {
    auto z = zero_i + 1 - zero_n;
    for (int i = 0; i < z; i++) write(ip[i]);
    str[p++] = ':';
    z += zero_n;
    if (z < 8) {
      for (int i = z; i < 8; i++) write(ip[i]);
    } else {
      str[p++] = ':';
    }
  } else {
    for (int i = 0; i < 8; i++) write(ip[i]);
  }

  return pjs::Str::make(str, p);
}

static inline auto mask_of(int bits) -> uint32_t {
  return bits >= 32 ? -1 : (1 << bits) - 1;
}

//
// Netmask
//

Netmask::Netmask(pjs::Str *cidr) : m_cidr(cidr) {
  char str[50];
  if (cidr->size() >= sizeof(str)) {
    throw std::runtime_error("string too long for CIDR notation");
  }

  std::strcpy(str, cidr->c_str());
  if (char *p = std::strchr(str, '/')) {
    *p++ = '\0';
    m_bitmask = std::atoi(p);
  } else {
    m_bitmask = 0;
  }

  uint8_t ipv4[4];
  if (utils::get_ip_v4(str, ipv4)) {
    if (m_bitmask < 0 || m_bitmask > 32) throw std::runtime_error("IPv4 CIDR mask out of range");
    m_is_v6 = false;
    m_ip_full.v4 = (
      ((uint32_t)ipv4[0] << 24) |
      ((uint32_t)ipv4[1] << 16) |
      ((uint32_t)ipv4[2] <<  8) |
      ((uint32_t)ipv4[3] <<  0)
    );
    m_ip_mask.v4 = mask_of(m_bitmask) << (32 - m_bitmask);
    m_ip_base.v4 = m_ip_full.v4 & m_ip_mask.v4;
  } else if (utils::get_ip_v6(str, m_ip_full.v6)) {
    if (m_bitmask < 0 || m_bitmask > 128) throw std::runtime_error("IPv6 CIDR mask out of range");
    m_is_v6 = true;
    for (int i = 0; i < 8; i++) {
      int n = std::min(m_bitmask - i * 16, 16);
      int m = (n <= 0 ? 0 : (mask_of(n) << (16 - n)));
      m_ip_mask.v6[i] = m;
      m_ip_base.v6[i] = m_ip_full.v6[i] & m;
    }
  } else {
    throw std::runtime_error("invalid CIDR notation");
  }
}

auto Netmask::ip() -> pjs::Str* {
  if (!m_ip) {
    if (m_is_v6) {
      m_ip = ip6_to_str(m_ip_full.v6);
    } else {
      m_ip = ip4_to_str(m_ip_full.v4);
    }
  }
  return m_ip;
}

auto Netmask::base() -> pjs::Str* {
  if (!m_base) {
    if (m_is_v6) {
      m_base = ip6_to_str(m_ip_base.v6);
    } else {
      m_base = ip4_to_str(m_ip_base.v4);
    }
  }
  return m_base;
}

auto Netmask::mask() -> pjs::Str* {
  if (!m_mask) {
    if (m_is_v6) {
      m_mask = ip6_to_str(m_ip_mask.v6);
    } else {
      m_mask = ip4_to_str(m_ip_mask.v4);
    }
  }
  return m_mask;
}

auto Netmask::hostmask() -> pjs::Str* {
  if (!m_hostmask) {
    if (m_is_v6) {
      IP ip; for (int i = 0; i < 8; i++) ip.v6[i] = ~m_ip_mask.v6[i];
      m_hostmask = ip6_to_str(ip.v6);
    } else {
      m_hostmask = ip4_to_str(~m_ip_mask.v4);
    }
  }
  return m_hostmask;
}

auto Netmask::broadcast() -> pjs::Str* {
  if (!m_broadcast) {
    if (m_is_v6) {
      IP ip; for (int i = 0; i < 8; i++) ip.v6[i] = m_ip_base.v6[i] | ~m_ip_mask.v6[i];
      m_broadcast = ip6_to_str(ip.v6);
    } else {
      m_broadcast = ip4_to_str(m_ip_base.v4 | ~m_ip_mask.v4);
    }
  }
  return m_broadcast;
}

auto Netmask::first() -> pjs::Str* {
  if (!m_first) {
    if (m_is_v6) {
      IP ip = m_ip_base;
      ip.v6[7] |= (~m_ip_mask.v6[7] & 1);
    } else {
      m_first = ip4_to_str(m_ip_base.v4 | (~m_ip_mask.v4 & 1));
    }
  }
  return m_first;
}

auto Netmask::decompose() -> pjs::Array* {
  if (m_is_v6) {
    auto *arr = pjs::Array::make(8);
    for (int i = 0; i < 8; i++) arr->set(i, m_ip_full.v6[i]);
    return arr;
  } else {
    auto *arr = pjs::Array::make(4);
    for (int i = 0; i < 4; i++) arr->set(i, (m_ip_full.v4 >> (24 - i * 8)) & 255);
    return arr;
  }
}

bool Netmask::decompose_v4(uint8_t ip[]) {
  if (m_is_v6) return false;
  for (int i = 0; i < 4; i++) {
    ip[i] = (m_ip_full.v4 >> (24 - i * 8)) & 255;
  }
  return true;
}

bool Netmask::decompose_v6(uint16_t ip[]) {
  if (!m_is_v6) return false;
  for (int i = 0; i < 8; i++) {
    ip[i] = m_ip_full.v6[i];
  }
  return true;
}

auto Netmask::last() -> pjs::Str* {
  if (!m_last) {
    if (m_is_v6) {
      IP ip; for (int i = 0; i < 7; i++) ip.v6[i] = m_ip_base.v6[i] | ~m_ip_mask.v6[i];
      auto mask = ~m_ip_mask.v6[7];
      ip.v6[7] = m_ip_base.v6[7] | (mask & (mask - 1));
      m_last = ip6_to_str(ip.v6);
    } else {
      auto mask = ~m_ip_mask.v4;
      m_last = ip4_to_str(m_ip_base.v4 | (mask & (mask - 1)));
    }
  }
  return m_last;
}

bool Netmask::contains(pjs::Str *addr) {
  if (m_is_v6) {
    IP ip;
    if (!utils::get_ip_v6(addr->str(), ip.v6)) {
      return false;
    }
    for (int i = 0; i < 8; i++) {
      if ((ip.v6[i] & m_ip_mask.v6[i]) != m_ip_base.v6[i]) {
        return false;
      }
    }
    return true;
  } else {
    uint8_t ip[4];
    if (!utils::get_ip_v4(addr->str(), ip)) {
      return false;
    }
    return (get_ip4(ip) & m_ip_mask.v4) == m_ip_base.v4;
  }
}

auto Netmask::next() -> pjs::Str* {
  if (m_is_v6) {
    uint64_t mask = (
      ((uint64_t)m_ip_mask.v6[4] << 48) |
      ((uint64_t)m_ip_mask.v6[5] << 32) |
      ((uint64_t)m_ip_mask.v6[6] << 16) |
      ((uint64_t)m_ip_mask.v6[7] <<  0)
    );
    if (m_next == ~mask) return pjs::Str::empty;
    auto n = m_next++;
    IP ip = m_ip_base;
    ip.v6[4] = n >> 48;
    ip.v6[5] = n >> 32;
    ip.v6[6] = n >> 16;
    ip.v6[7] = n >>  0;
    return ip6_to_str(ip.v6);
  } else {
    if (m_next == ~m_ip_mask.v4) return pjs::Str::empty;
    auto n = m_next++;
    return ip4_to_str(m_ip_base.v4 | n);
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// Netmask
//

template<> void ClassDef<Netmask>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *cidr;
    if (!ctx.arguments(1, &cidr)) return nullptr;
    try {
      return Netmask::make(cidr);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  accessor("version",   [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->version()); });
  accessor("ip",        [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->ip()); });
  accessor("bitmask",   [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->bitmask()); });
  accessor("base",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->base()); });
  accessor("mask",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->mask()); });
  accessor("hostmask",  [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->hostmask()); });
  accessor("broadcast", [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->broadcast()); });
  accessor("size",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->size()); });
  accessor("first",     [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->first()); });
  accessor("last",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->last()); });

  method("decompose", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Netmask>()->decompose());
  });

  method("contains", [](Context &ctx, Object *obj, Value &ret) {
    Str *addr;
    if (!ctx.arguments(1, &addr)) return;
    ret.set(obj->as<Netmask>()->contains(addr));
  });

  method("next", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Netmask>()->next());
  });
}

template<> void ClassDef<Constructor<Netmask>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
