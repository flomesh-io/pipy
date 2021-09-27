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

static inline auto mask_of(int bits) -> uint32_t {
  return bits >= 32 ? -1 : (1 << bits) - 1;
}

//
// Netmask
//

Netmask::Netmask(pjs::Str *cidr) : m_cidr(cidr) {
  uint8_t ip[4];
  if (!utils::get_cidr(cidr->str(), ip, m_bitmask)) {
    throw std::runtime_error("invalid CIDR notation");
  }

  m_ip4_mask = mask_of(m_bitmask) << (32 - m_bitmask);
  m_ip4_base = get_ip4(ip) & m_ip4_mask;
}

auto Netmask::base() -> pjs::Str* {
  if (!m_base) m_base = ip4_to_str(m_ip4_base);
  return m_base;
}

auto Netmask::mask() -> pjs::Str* {
  if (!m_mask) m_mask = ip4_to_str(m_ip4_mask);
  return m_mask;
}

auto Netmask::hostmask() -> pjs::Str* {
  if (!m_hostmask) m_hostmask = ip4_to_str(~m_ip4_mask);
  return m_hostmask;
}

auto Netmask::broadcast() -> pjs::Str* {
  if (!m_broadcast) m_broadcast = ip4_to_str(m_ip4_base | ~m_ip4_mask);
  return m_broadcast;
}

auto Netmask::first() -> pjs::Str* {
  if (!m_first) m_first = ip4_to_str(m_ip4_base | (~m_ip4_mask & 1));
  return m_first;
}

auto Netmask::last() -> pjs::Str* {
  if (!m_last) {
    auto mask = ~m_ip4_mask;
    m_last = ip4_to_str(m_ip4_base | (mask & (mask - 1)));
  }
  return m_last;
}

bool Netmask::contains(pjs::Str *addr) {
  uint8_t ip[4];
  if (!utils::get_ip_v4(addr->str(), ip)) {
    return false;
  }
  return (get_ip4(ip) & m_ip4_mask) == m_ip4_base;
}

auto Netmask::next() -> pjs::Str* {
  if (m_next == ~m_ip4_mask) return pjs::Str::empty.get();
  auto n = m_next++;
  return ip4_to_str(m_ip4_base | n);
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

  accessor("base",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->base()); });
  accessor("mask",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->mask()); });
  accessor("bitmask",   [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->bitmask()); });
  accessor("hostmask",  [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->hostmask()); });
  accessor("broadcast", [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->broadcast()); });
  accessor("size",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->size()); });
  accessor("first",     [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->first()); });
  accessor("last",      [](Object *obj, Value &ret) { ret.set(obj->as<Netmask>()->last()); });

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