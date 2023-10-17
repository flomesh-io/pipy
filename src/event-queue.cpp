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

#include "event-queue.hpp"

namespace pipy {

SharedTable<EventQueue::SharedEvent> EventQueue::s_event_pool;

//
// EventQueue
//

EventQueue::EventQueue() {
  auto node = alloc(nullptr);
  m_head = node;
  m_tail = node;
}

EventQueue::~EventQueue() {
}

void EventQueue::enqueue(Event *evt) {
  auto node = alloc(evt);
  for (;;) {
    auto tail = m_tail.load();
    auto next = tail->next.load();
    if (tail == m_tail.load()) {
      if (!next) {
        if (tail->next.compare_exchange_weak(next, node.with_count(next.count() + 1))) {
          m_tail.compare_exchange_weak(tail, node.with_count(tail.count() + 1));
          break;
        }
      } else {
        m_tail.compare_exchange_weak(tail, next.with_count(tail.count() + 1));
      }
    }
  }
}

auto EventQueue::dequeue() -> Event* {
  for (;;) {
    auto head = m_head.load();
    auto tail = m_tail.load();
    auto next = head->next.load();
    if (head == m_head.load()) {
      if (head.index() == tail.index()) {
        if (!next) return nullptr;
        m_tail.compare_exchange_weak(tail, next.with_count(tail.count() + 1));
      } else {
        auto e = next->to_event();
        if (m_head.compare_exchange_weak(head, next.with_count(head.count() + 1))) {
          free(head);
          return e;
        } else {
          e->retain();
          e->release();
        }
      }
    }
  }
}

//
// EventQueue::SharedEvent
//

EventQueue::SharedEvent::SharedEvent(Event *evt)
  : type(evt ? evt->type() : (Event::Type)-1)
{
  switch (type) {
    case Event::Type::Data:
      data = SharedData::make(*evt->as<Data>());
      break;
    case Event::Type::MessageStart:
      head_tail = pjs::SharedObject::make(evt->as<MessageStart>()->head());
      break;
    case Event::Type::MessageEnd:
      head_tail = pjs::SharedObject::make(evt->as<MessageEnd>()->tail());
      new (&payload) pjs::SharedValue(evt->as<MessageEnd>()->payload());
      break;
    case Event::Type::StreamEnd:
      error_code = evt->as<StreamEnd>()->error_code();
      error = evt->as<StreamEnd>()->error();
      break;
    default: break;
  }
}

auto EventQueue::SharedEvent::to_event() -> Event* {
  switch (type) {
    case Event::Type::Data: {
      auto d = Data::make();
      data->to_data(*d);
      return d;
    }
    case Event::Type::MessageStart: {
      return MessageStart::make(head_tail->to_object());
    }
    case Event::Type::MessageEnd: {
      pjs::Value p;
      payload.to_value(p);
      return MessageEnd::make(head_tail->to_object(), p);
    }
    case Event::Type::StreamEnd: {
      if (error_code == StreamEnd::RUNTIME_ERROR) {
        pjs::Value e;
        error.to_value(e);
        return StreamEnd::make(e);
      } else {
        return StreamEnd::make(error_code);
      }
    }
    default: return nullptr;
  }
}

} // namespace pipy
