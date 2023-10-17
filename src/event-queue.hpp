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

#ifndef EVENT_QUEUE_HPP
#define EVENT_QUEUE_HPP

#include "event.hpp"
#include "data.hpp"
#include "table.hpp"
#include "net.hpp"

#include <atomic>

namespace pipy {

//
// EventQueue
//

class EventQueue : public pjs::Pooled<EventQueue> {
public:
  static auto make() -> EventQueue* {
    return new EventQueue;
  }

  auto retain() -> EventQueue* { m_refs.fetch_add(1, std::memory_order_relaxed); return this; }
  void release() { if (m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }
  void enqueue(Event *evt);
  auto dequeue() -> Event*;

private:
  struct SharedEvent;

  //
  // EventQueue::SharedEventPtr
  //

  class SharedEventPtr {
  public:
    SharedEventPtr(uint32_t index = 0, uint32_t count = 0)
      : m_index_count(((uint64_t)count << 32) | index) {}

    auto index() const -> uint32_t {
      return m_index_count;
    }

    auto count() const -> uint32_t {
      return m_index_count >> 32;
    }

    auto with_count(uint32_t count) -> SharedEventPtr {
      return SharedEventPtr(index(), count);
    }

    operator SharedEvent*() const {
      return s_event_pool.get(index());
    }

    operator bool() const {
      return index();
    }

    auto operator->() const -> SharedEvent* {
      return s_event_pool.get(index());
    }

    bool operator==(const SharedEventPtr &r) const {
      return m_index_count == r.m_index_count;
    }

  private:
    uint64_t m_index_count;
  };

  //
  // EventQueue::SharedEvent
  //

  struct SharedEvent {
    SharedEvent(Event *evt);
    auto to_event() -> Event*;
    Event::Type type;
    StreamEnd::Error error_code;
    pjs::SharedValue error;
    pjs::SharedValue payload;
    pjs::Ref<pjs::SharedObject> head_tail;
    pjs::Ref<SharedData> data;
    std::atomic<SharedEventPtr> next;
  };

  EventQueue();
  ~EventQueue();

  auto alloc(Event *evt) -> SharedEventPtr {
    return s_event_pool.alloc(evt);
  }

  void free(const SharedEventPtr &ptr) {
    s_event_pool.free(ptr.index());
  }

  std::atomic<int> m_refs;
  std::atomic<SharedEventPtr> m_head;
  std::atomic<SharedEventPtr> m_tail;

  static SharedTable<SharedEvent> s_event_pool;
};

} // namespace pipy

#endif // EVENT_QUEUE_HPP
