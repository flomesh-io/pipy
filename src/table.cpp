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

#include "table.hpp"

#include <cstring>

namespace pipy {

SharedTableBase::SharedTableBase(size_t entry_size)
  : m_entry_size(entry_size)
  , m_max_id(0)
  , m_free_id(0)
{
  std::memset(m_ranges, 0, sizeof(m_ranges));
}

auto SharedTableBase::get_entry(int i) -> Entry* {
  int x, y, z;
  index_to_xyz(i, x, y, z);
  auto r = m_ranges[x].load(std::memory_order_relaxed); if (!r) return nullptr;
  auto c = r->chunks[y].load(std::memory_order_relaxed); if (!c) return nullptr;
  return reinterpret_cast<Entry*>(c + z * m_entry_size);
}

auto SharedTableBase::add_entry(int i) -> Entry* {
  int x, y, z;
  index_to_xyz(i, x, y, z);
  auto r = m_ranges[x].load(std::memory_order_relaxed);
  if (!r) {
    auto *p = new Range;
    std::memset(p, 0, sizeof(Range));
    if (m_ranges[x].compare_exchange_weak(r, p, std::memory_order_relaxed)) {
      r = p;
    } else {
      delete p;
    }
  }
  auto c = r->chunks[y].load();
  if (!c) {
    auto size = 256 * m_entry_size;
    auto *p = new char[size];
    std::memset(p, 0, size);
    if (r->chunks[y].compare_exchange_weak(c, p, std::memory_order_relaxed)) {
      c = p;
    } else {
      delete [] p;
    }
  }
  return reinterpret_cast<Entry*>(c + z * m_entry_size);
}

auto SharedTableBase::alloc_entry() -> Entry* {
  auto i_npop = m_free_id.load(std::memory_order_relaxed);
  while (auto i = uint32_t(i_npop)) {
    auto e = get_entry(i);
    auto npop = uint32_t(i_npop >> 32);
    auto i_npop_new = (uint64_t(npop+1) << 32) | e->m_next_free;
    if (m_free_id.compare_exchange_weak(
      i_npop, i_npop_new,
      std::memory_order_acquire,
      std::memory_order_relaxed
    )) {
      e->index = i;
      e->m_hold_count.store(1, std::memory_order_relaxed);
      return e;
    }
  }
  auto i = m_max_id.fetch_add(1, std::memory_order_relaxed) + 1;
  auto e = add_entry(i);
  e->index = i;
  e->m_hold_count.store(1, std::memory_order_relaxed);
  return e;
}

void SharedTableBase::free_entry(Entry *e) {
  int i = e->index;
  uint64_t i_npop = m_free_id.load(std::memory_order_relaxed);
  uint64_t i_npop_new;
  do {
    e->m_next_free = uint32_t(i_npop);
    i_npop_new = (i_npop & (~uint64_t(0) << 32)) | uint32_t(i);
  }
  while (!m_free_id.compare_exchange_weak(
    i_npop, i_npop_new,
    std::memory_order_release,
    std::memory_order_relaxed
  ));
}

} // namespace pipy
