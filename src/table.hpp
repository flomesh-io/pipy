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

#ifndef PIPY_TABLE_HPP
#define PIPY_TABLE_HPP

#include <cstddef>
#include <cstring>
#include <atomic>
#include <vector>

namespace pipy {

//
// TableBase
//

class TableBase {
private:
  enum { SUB_TABLE_WIDTH = 8 };

  struct Entry {
    int next_free = 0;
    char data[0];
  };

  std::vector<char*> m_sub_tables;

  int m_size = 0;
  int m_free = 0;
  int m_data_size;

protected:
  TableBase(int data_size)
    : m_data_size(data_size) {}

  Entry* get_entry(int i, bool create) {
    int y = i & ((1 << SUB_TABLE_WIDTH) - 1);
    int x = i >> SUB_TABLE_WIDTH;
    if (x >= m_sub_tables.size()) {
      if (!create) return nullptr;
      m_sub_tables.resize(x + 1);
    }
    auto sub = m_sub_tables[x];
    if (!sub) {
      if (!create) return nullptr;
      auto *buf = new char[(sizeof(Entry) + m_data_size) * (1 << SUB_TABLE_WIDTH)];
      m_sub_tables[x] = sub = buf;
    }
    return reinterpret_cast<Entry*>(sub + (sizeof(Entry) + m_data_size) * y);
  }

  Entry* alloc_entry(int *i) {
    Entry *ent;
    auto id = m_free;
    if (!id) {
      id = ++m_size;
      ent = get_entry(id, true);
    } else {
      ent = get_entry(id, false);
      m_free = ent->next_free;
    }
    ent->next_free = -1;
    *i = id;
    return ent;
  }

  Entry* free_entry(int i) {
    if (auto *ent = get_entry(i, false)) {
      if (ent->next_free < 0) {
        ent->next_free = m_free;
        m_free = i;
        return ent;
      }
    }
    return nullptr;
  }
};

//
// Table
//

template<class T>
class Table : public TableBase {
public:
  Table() : TableBase(sizeof(T)) {}

  T* get(int i) {
    if (i <= 0) return nullptr;
    if (auto *ent = get_entry(i, false)) {
      return reinterpret_cast<T*>(ent->data);
    } else {
      return nullptr;
    }
  }

  template<typename... Args>
  int alloc(Args... args) {
    int i;
    auto *ent = alloc_entry(&i);
    new (ent->data) T(std::forward<Args>(args)...);
    return i;
  }

  void free(int i) {
    if (auto *ent = free_entry(i)) {
      ((T*)ent->data)->~T();
    }
  }
};

//
// SharedTableBase
//

class SharedTableBase {
protected:

  //
  // SharedTableBase::Entry
  //

  struct Entry {
    int index;
    void hold() { m_hold_count.fetch_add(1, std::memory_order_relaxed); }
    bool release() { return m_hold_count.fetch_sub(1, std::memory_order_relaxed) == 1; }
  protected:
    Entry() {}
  private:
    std::atomic<int> m_hold_count;
    uint32_t m_next_free = 0;
    friend class SharedTableBase;
  };

  SharedTableBase(size_t entry_size);

  auto get_entry(int i) -> Entry*;
  auto add_entry(int i) -> Entry*;
  auto alloc_entry() -> Entry*;
  void free_entry(Entry *e);

private:
  struct Range {
    std::atomic<char*> chunks[256];
  };

  size_t m_entry_size;
  std::atomic<Range*> m_ranges[256];
  std::atomic<uint32_t> m_max_id;
  std::atomic<uint64_t> m_free_id;

  static void index_to_xyz(int i, int &x, int &y, int &z) {
    z = 0xff & (i >> 0);
    y = 0xff & (i >> 8);
    x = 0xff & (i >> 16);
  }
};

//
// SharedTable
//

template<class T>
class SharedTable : public SharedTableBase {
public:

  //
  // SharedTable::Entry
  //

  struct Entry : public SharedTableBase::Entry {
    T data;
  };

  SharedTable() : SharedTableBase(sizeof(Entry)) {}

  T* get(int i) {
    auto *e = static_cast<Entry*>(get_entry(i));
    return e ? &e->data : nullptr;
  }

  template<typename... Args>
  int alloc(Args... args) {
    auto e = static_cast<Entry*>(alloc_entry());
    new (&e->data) T(std::forward<Args>(args)...);
    return e->index;
  }

  void free(int i) {
    auto e = static_cast<Entry*>(get_entry(i));
    if (e->release()) {
      e->data.~T();
      std::memset(&e->data, 0, sizeof(T));
      free_entry(e);
    }
  }
};

} // namespace pipy

#endif // PIPY_TABLE_HPP
