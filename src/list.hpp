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

#ifndef LIST_HPP
#define LIST_HPP

namespace pipy {

template<class T>
class List {
public:
  class Item {
  public:
    auto back() const -> T* { return static_cast<T*>(m_back); }
    auto next() const -> T* { return static_cast<T*>(m_next); }

  private:
    Item* m_back = nullptr;
    Item* m_next = nullptr;

    friend class List;
  };

  bool empty() const { return !m_head; }
  auto size() const -> size_t { return m_size; }
  auto head() const -> T* { return static_cast<T*>(m_head); }
  auto tail() const -> T* { return static_cast<T*>(m_tail); }

  void push(Item *item) {
    item->m_back = m_tail;
    if (m_tail) {
      m_tail->m_next = item;
      m_tail = item;
    } else {
      m_tail = m_head = item;
    }
    m_size++;
  }

  void remove(Item *item) {
    if (item->m_next) item->m_next->m_back = item->m_back; else m_tail = item->m_back;
    if (item->m_back) item->m_back->m_next = item->m_next; else m_head = item->m_next;
    item->m_back = item->m_next = nullptr;
    m_size--;
  }

private:
  Item* m_head = nullptr;
  Item* m_tail = nullptr;
  size_t m_size = 0;
};

} // namespace pipy

#endif // LIST_HPP