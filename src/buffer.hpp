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

#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "data.hpp"
#include "list.hpp"
#include "options.hpp"

#include <functional>
#include <memory>
#include <string>

namespace pipy {

//
// BufferStats
//

struct BufferStats : public List<BufferStats>::Item {
  std::string name;
  size_t size;

  BufferStats() { s_all.push(this); }
  ~BufferStats() { s_all.remove(this); }

  static void for_each(const std::function<void(BufferStats*)> &callback) {
    for (auto i = s_all.head(); i; i = i->next()) {
      callback(i);
    }
  }

private:
  thread_local static List<BufferStats> s_all;
};

//
// EventBuffer
//

class EventBuffer {
public:
  EventBuffer(std::shared_ptr<BufferStats> stats = nullptr)
    : m_stats(stats) {}

  EventBuffer(const EventBuffer &r)
    : m_stats(r.m_stats) {}

  bool empty() const {
    return m_events.empty();
  }

  void push(Event *e) {
    if (e->m_in_buffer) e = e->clone();
    e->m_in_buffer = true;
    e->retain();
    m_events.push(e);
    if (m_stats) {
      if (auto data = e->as<Data>()) {
        m_stats->size += data->size();
      }
    }
  }

  auto shift() -> Event* {
    if (m_events.empty()) return nullptr;
    auto e = m_events.head();
    m_events.remove(e);
    e->m_in_buffer = false;
    if (m_stats) {
      if (auto data = e->as<Data>()) {
        m_stats->size -= data->size();
      }
    }
    return e;
  }

  void unshift(Event *e) {
    if (e->m_in_buffer) e = e->clone();
    e->m_in_buffer = true;
    e->retain();
    m_events.unshift(e);
    if (m_stats) {
      if (auto data = e->as<Data>()) {
        m_stats->size += data->size();
      }
    }
  }

  void iterate(const std::function<void(Event*)> &cb) {
    for (auto e = m_events.head(); e; e = e->next()) {
      cb(e);
    }
  }

  void flush(EventTarget::Input *input) {
    List<Event> events(std::move(m_events));
    while (auto e = events.head()) {
      events.remove(e);
      e->m_in_buffer = false;
      if (m_stats) {
        if (auto data = e->as<Data>()) {
          m_stats->size += data->size();
        }
      }
      input->input(e);
      e->release();
    }
  }

  void flush(const std::function<void(Event*)> &out) {
    List<Event> events(std::move(m_events));
    while (auto e = events.head()) {
      events.remove(e);
      e->m_in_buffer = false;
      out(e);
      e->release();
    }
  }

  void clear() {
    List<Event> events(std::move(m_events));
    while (auto e = events.head()) {
      events.remove(e);
      e->m_in_buffer = false;
      e->release();
    }
  }

private:
  List<Event> m_events;
  std::shared_ptr<BufferStats> m_stats;
};

//
// DataBuffer
//

class DataBuffer {
public:

  //
  // DataBuffer::Options
  //

  struct Options : public pipy::Options {
    int bufferLimit = -1;
    Options() {}
    Options(pjs::Object *options);
  };

  DataBuffer(const std::shared_ptr<BufferStats> stats = nullptr)
    : m_stats(stats) {}

  DataBuffer(
    const Options &options,
    const std::shared_ptr<BufferStats> stats = nullptr
  ) : m_options(options)
    , m_stats(stats) {}

  DataBuffer(const DataBuffer &r)
    : m_options(r.m_options)
    , m_stats(r.m_stats) {}

  void clear();
  bool empty() const { return m_buffer.empty(); }
  auto size() const -> int { return m_buffer.size(); }
  void push(const Data &data);
  auto flush() -> Data*;
  void flush(Data &out);

private:
  Options m_options;
  std::shared_ptr<BufferStats> m_stats;
  Data m_buffer;
};

} // namespace pipy

#endif // BUFFER_HPP
