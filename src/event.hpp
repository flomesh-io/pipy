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

#ifndef EVENT_HPP
#define EVENT_HPP

#include "pjs/pjs.hpp"
#include "list.hpp"

namespace pipy {

class EventBuffer;

//
// Event
//

class Event :
  public pjs::ObjectTemplate<Event>,
  public List<Event>::Item
{
public:
  enum class Type {
    Data,
    MessageStart,
    MessageEnd,
    StreamEnd,
  };

  auto type() const -> Type { return m_type; }
  bool is_end() const { return m_type == Type::MessageEnd || m_type == Type::StreamEnd; }

  virtual auto clone() const -> Event* = 0;

  template<class T> auto is() const -> bool {
    return m_type == T::__TYPE;
  }

  template<class T> auto as() const -> const T* {
    return is<T>() ? static_cast<const T*>(this) : nullptr;
  }

  template<class T> auto as() -> T* {
    return is<T>() ? static_cast<T*>(this) : nullptr;
  }

protected:
  Event(Type type) : m_type(type) {}

  virtual ~Event() {}

private:
  Type m_type;
  bool m_in_buffer = false;

  friend class pjs::ObjectTemplate<Event>;
  friend class EventBuffer;
};

//
// EventTemplate
//

template<class T>
class EventTemplate : public pjs::ObjectTemplate<T, Event> {
protected:
  EventTemplate()
    : pjs::ObjectTemplate<T, Event>(T::__TYPE) {}

  virtual auto clone() const -> Event* override {
    return T::make(*static_cast<const T*>(this));
  }
};

//
// MessageStart
//

class MessageStart : public EventTemplate<MessageStart> {
public:
  static const Type __TYPE = Type::MessageStart;

  auto head() const -> pjs::Object* { return m_head; }

private:
  MessageStart() {}

  MessageStart(pjs::Object *head)
    : m_head(head) {}

  MessageStart(const MessageStart &r)
    : m_head(r.m_head) {}

  pjs::Ref<pjs::Object> m_head;

  friend class pjs::ObjectTemplate<MessageStart, Event>;
};

//
// MessageEnd
//

class MessageEnd : public EventTemplate<MessageEnd> {
public:
  static const Type __TYPE = Type::MessageEnd;

  auto tail() const -> pjs::Object* { return m_tail; }
  auto payload() const -> const pjs::Value& { return m_payload; }

private:
  MessageEnd() {}

  MessageEnd(pjs::Object *tail)
    : m_tail(tail) {}

  MessageEnd(pjs::Object *tail, const pjs::Value &payload)
    : m_tail(tail)
    , m_payload(payload) {}

  MessageEnd(const MessageEnd &r)
    : m_tail(r.m_tail)
    , m_payload(r.m_payload) {}

  pjs::Ref<pjs::Object> m_tail;
  pjs::Value m_payload;

  friend class pjs::ObjectTemplate<MessageEnd, Event>;
};

//
// StreamEnd
//

class StreamEnd : public EventTemplate<StreamEnd> {
public:
  static const Type __TYPE = Type::StreamEnd;

  enum Error {
    NO_ERROR = 0,
    REPLAY,
    RUNTIME_ERROR,
    READ_ERROR,
    WRITE_ERROR,
    CANNOT_RESOLVE,
    CONNECTION_CANCELED,
    CONNECTION_RESET,
    CONNECTION_REFUSED,
    CONNECTION_TIMEOUT,
    READ_TIMEOUT,
    WRITE_TIMEOUT,
    IDLE_TIMEOUT,
    BUFFER_OVERFLOW,
    PROTOCOL_ERROR,
    UNAUTHORIZED,
  };

  auto error() const -> const pjs::Value & { return m_error; }
  auto error_code() const -> Error { return m_error_code; }
  bool has_error() const { return !m_error.is_undefined() || m_error_code != Error::NO_ERROR; }

private:
  StreamEnd(Error error_code = NO_ERROR) : m_error_code(error_code) {}
  StreamEnd(const pjs::Value &error) : m_error(error), m_error_code(RUNTIME_ERROR) {}

  StreamEnd(const StreamEnd &r)
    : m_error(r.m_error)
    , m_error_code(r.m_error_code) {}

  pjs::Value m_error;
  pjs::EnumValue<Error> m_error_code;

  friend class pjs::ObjectTemplate<StreamEnd, Event>;
};

//
// EventTarget
//
//      input()      +-------------+
// --- on_event() -->| EventTarget |
//                   +-------------+
//

class EventTarget {
public:

  //
  // EventTarget::Input
  //

  class Input : public pjs::RefCount<Input> {
  public:
    static auto dummy() -> Input*;
    static auto make(Input *input) -> Input*;

    virtual void input(Event *evt) = 0;
    virtual void close() = 0;

  protected:
    virtual ~Input() {}

    static auto make(EventTarget *target) -> Input*;

    friend class pjs::RefCount<Input>;
    friend class EventTarget;
  };

  auto input() -> Input* {
    if (!m_input) {
      m_input = Input::make(this);
    }
    return m_input;
  }

  void close() {
    if (m_input) {
      m_input->close();
      m_input = nullptr;
    }
  }

protected:
  ~EventTarget() {
    close();
  }

  virtual void on_event(Event *evt) {}

private:
  pjs::Ref<Input> m_input;

  //
  // EventTarget::TargetInput
  //

  class TargetInput : public pjs::Pooled<TargetInput>, public Input {
  public:
    TargetInput(EventTarget *target)
      : m_target(target) {}

  private:
    EventTarget* m_target;

    virtual void input(Event *evt) override {
      pjs::Ref<Event> ref(evt);
      if (m_target) {
        m_target->on_event(evt);
      }
    }

    virtual void close() override {
      m_target = nullptr;
    }
  };

  //
  // EventTarget::InputInput
  //

  class InputInput : public pjs::Pooled<InputInput>, public Input {
  public:
    InputInput(Input *input)
      : m_input(input) {}

  private:
    pjs::Ref<Input> m_input;

    virtual void input(Event *evt) override {
      pjs::Ref<Event> ref(evt);
      if (m_input) {
        m_input->input(evt);
      }
    }

    virtual void close() override {
      m_input = nullptr;
    }
  };

  //
  // EventTarget::DummyInput
  //

  class DummyInput : public Input {
    virtual void input(Event *evt) override {
      pjs::Ref<Event> ref(evt);
    }

    virtual void close() override {}
  };

  friend class Input;
};

//
// EventFunction
//
//      input()      +---------------+
// --- on_input() -->|               |
//                   | EventFunction |
// <--- output() ----|               |
//                   +---------------+
//

class EventFunction : public EventTarget {
public:
  virtual void chain(Input *input) {
    if (input) {
      m_output = input;
    } else {
      m_output = Input::dummy();
    }
  }

protected:
  EventFunction()
    : m_output(Input::dummy()) {}

  virtual void on_input(Event *evt) {}
  virtual void on_event(Event *evt) override { on_input(evt); }

  auto output() -> Input* {
    return m_output;
  }

  void output(Event *evt) {
    m_output->input(evt);
  }

  void output(Event *evt, EventTarget::Input *input) {
    if (input) {
      input->input(evt);
    }
  }

private:
  pjs::Ref<Input> m_output;
};

//
// EventSource
//
// +-------------+
// |             |---- output() --->
// | EventSource |
// |             |<-- on_reply() ---
// +-------------+      reply()
//

class EventSource : protected EventTarget {
public:
  auto reply() -> Input* {
    return EventTarget::input();
  }

  void chain(Input *input) {
    if (input) {
      m_output = input;
    } else {
      m_output = Input::dummy();
    }
  }

  void close() {
    EventTarget::close();
  }

protected:
  EventSource()
    : m_output(Input::dummy()) {}

  virtual void on_reply(Event *evt) {}
  virtual void on_event(Event *evt) override { on_reply(evt); }

  auto output() -> Input* {
    return m_output;
  }

  void output(Event *evt) {
    m_output->input(evt);
  }

  void output(Event *evt, EventTarget::Input *input) {
    if (input) {
      input->input(evt);
    }
  }

private:
  pjs::Ref<Input> m_output;
};

//
// EventProxy
//
//      input()      +------------+
// --- on_input() -->|            |--- forward() --->
//                   | EventProxy |
// <--- output() ----|            |<-- on_reply() ---
//                   +------------+     reply()
//

class EventProxy : public EventFunction, protected EventSource {
public:
  auto input() -> Input* {
    return EventFunction::input();
  }

  virtual void chain(Input *input) override {
    EventFunction::chain(input);
  }

  void chain_forward(Input *input) {
    EventSource::chain(input);
  }

  void close() {
    EventFunction::close();
    EventSource::close();
  }

protected:
  auto forward() -> Input* {
    return EventSource::output();
  }

  void forward(Event *evt) {
    EventSource::output(evt);
  }

  void forward(Event *evt, EventTarget::Input *input) {
    EventSource::output(evt, input);
  }

  auto output() -> Input* {
    return EventFunction::output();
  }

  void output(Event *evt) {
    EventFunction::output(evt);
  }

  void output(Event *evt, EventTarget::Input *input) {
    EventFunction::output(evt, input);
  }
};

//
// EventBuffer
//

class EventBuffer {
public:
  bool empty() const {
    return m_events.empty();
  }

  void push(Event *e) {
    if (e->m_in_buffer) e = e->clone();
    e->m_in_buffer = true;
    e->retain();
    m_events.push(e);
  }

  auto shift() -> Event* {
    if (m_events.empty()) return nullptr;
    auto e = m_events.head();
    m_events.remove(e);
    e->m_in_buffer = false;
    return e;
  }

  void unshift(Event *e) {
    if (e->m_in_buffer) e = e->clone();
    e->m_in_buffer = true;
    e->retain();
    m_events.unshift(e);
  }

  void iterate(const std::function<void(Event*)> &cb) {
    for (auto e = m_events.head(); e; e = e->next()) {
      cb(e);
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
};

} // namespace pipy

#endif // EVENT_HPP
