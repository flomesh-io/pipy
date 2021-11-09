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
// Netmask
//

class Netmask : public pjs::ObjectTemplate<Netmask> {
public:
  auto base() -> pjs::Str*;
  auto mask() -> pjs::Str*;
  auto bitmask() const -> int { return m_bitmask; }
  auto hostmask() -> pjs::Str*;
  auto broadcast() -> pjs::Str*;
  auto size() const -> int { return 1 << (32 - m_bitmask); }
  auto first() -> pjs::Str*;
  auto last() -> pjs::Str*;
  bool contains(pjs::Str *addr);
  auto next() -> pjs::Str*;

  virtual auto to_string() const -> std::string override {
    return m_cidr->str();
  }

private:
  Netmask(pjs::Str *cidr);

  pjs::Ref<pjs::Str> m_cidr;
  pjs::Ref<pjs::Str> m_base;
  pjs::Ref<pjs::Str> m_mask;
  pjs::Ref<pjs::Str> m_hostmask;
  pjs::Ref<pjs::Str> m_broadcast;
  pjs::Ref<pjs::Str> m_first;
  pjs::Ref<pjs::Str> m_last;

  int m_bitmask;
  uint32_t m_next = 1;
  uint32_t m_ip4_base;
  uint32_t m_ip4_mask;

  friend class pjs::ObjectTemplate<Netmask>;
};

} // namespace pipy

#endif // NETMASK_HPP
