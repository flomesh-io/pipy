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

#ifndef NETMASK_HPP
#define NETMASK_HPP

#include "pjs/pjs.hpp"

namespace pipy {

//
// IPAddressData
//

class IPAddressData {
public:
  IPAddressData() {}
  IPAddressData(uint32_t data) { set_v4(data); }
  IPAddressData(const uint16_t data[]) { set_v6(data); }
  bool is_v6() const { return m_is_v6; }
  auto v4() const -> uint32_t { return m_data.v4; }
  auto v6() const -> const uint16_t* { return m_data.v6; }
  void set_v4(uint32_t data);
  void set_v6(const uint16_t data[]);
  bool decompose_v4(uint8_t data[]);
  bool decompose_v6(uint16_t data[]);
  auto decompose() -> pjs::Array*;
  auto to_bytes() -> pjs::Array*;
  auto to_string(char *str, size_t len) -> size_t;
  auto to_string() -> pjs::Str*;

private:
  union {
    uint32_t v4;
    uint16_t v6[8];
  } m_data;
  pjs::Ref<pjs::Str> m_str;
  bool m_is_v6;
};

//
// Netmask
//

class Netmask : public pjs::ObjectTemplate<Netmask> {
public:
  auto version() const -> int { return m_ip_full.is_v6() ? 6 : 4; }
  auto ip() -> pjs::Str* { return m_ip_full.to_string(); }
  auto bitmask() const -> int { return m_bitmask; }
  auto base() -> pjs::Str* { return m_ip_base.to_string(); }
  auto mask() -> pjs::Str* { return m_ip_mask.to_string(); }
  auto hostmask() -> pjs::Str*;
  auto broadcast() -> pjs::Str*;
  auto size() const -> double;
  auto first() -> pjs::Str*;
  auto last() -> pjs::Str*;
  bool contains(pjs::Str *addr);
  auto next() -> pjs::Str*;
  bool decompose_v4(uint8_t data[]) { return m_ip_full.decompose_v4(data); }
  bool decompose_v6(uint16_t data[]) { return m_ip_full.decompose_v6(data); }
  auto decompose() -> pjs::Array* { return m_ip_full.decompose(); }
  auto to_bytes() -> pjs::Array* { return m_ip_full.to_bytes(); }

  virtual auto to_string() const -> std::string override {
    return m_cidr->str();
  }

private:
  Netmask(pjs::Str *cidr);
  Netmask(int mask, uint32_t ipv4);
  Netmask(int mask, uint8_t ipv4[]);
  Netmask(int mask, uint16_t ipv6[]);

  pjs::Ref<pjs::Str> m_cidr;
  pjs::Ref<pjs::Str> m_hostmask;
  pjs::Ref<pjs::Str> m_broadcast;
  pjs::Ref<pjs::Str> m_first;
  pjs::Ref<pjs::Str> m_last;

  int m_bitmask;
  IPAddressData m_ip_full;
  IPAddressData m_ip_base;
  IPAddressData m_ip_mask;
  uint64_t m_next = 1;

  void init_mask();

  friend class pjs::ObjectTemplate<Netmask>;
};

} // namespace pipy

#endif // NETMASK_HPP
