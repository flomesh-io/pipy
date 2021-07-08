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

#include <functional>
#include <memory>
#include <string>

namespace pipy {

//
// Event base
//

class Event : public pjs::ObjectTemplate<Event> {
public:
  typedef std::function<void(Event*)> Receiver;

  enum Type {
    Data,
    MessageStart,
    MessageEnd,
    SessionEnd,
  };

  virtual ~Event() {}
  virtual auto type() const -> Type = 0;
  virtual auto name() const -> const char* = 0;
  virtual auto clone() const -> Event* = 0;

  template<class T> auto is() const -> bool {
    return dynamic_cast<const T*>(this) != nullptr;
  }

  template<class T> auto as() const -> const T* {
    return dynamic_cast<const T*>(this);
  }

  template<class T> auto as() -> T* {
    return dynamic_cast<T*>(this);
  }
};

//
// MessageStart
//

class MessageStart : public pjs::ObjectTemplate<MessageStart, Event> {
public:
  auto context() -> pjs::Object* { return m_context; }
  auto head() -> pjs::Object* { return m_head; }

private:
  MessageStart() {}

  MessageStart(pjs::Object *head)
    : m_context(pjs::Object::make())
    , m_head(head) {}

  MessageStart(pjs::Object *context, pjs::Object *head)
    : m_context(context)
    , m_head(head) {}

  MessageStart(const MessageStart &r)
    : m_context(r.m_context)
    , m_head(r.m_head) {}

  pjs::Ref<pjs::Object> m_context;
  pjs::Ref<pjs::Object> m_head;

  virtual auto type() const -> Type override { return Event::MessageStart; }
  virtual auto name() const -> const char* override { return "MessageStart"; }
  virtual auto clone() const -> Event* override { return make(*this); }

  friend class pjs::ObjectTemplate<MessageStart, Event>;
};

//
// MessageEnd
//

struct MessageEnd : public pjs::ObjectTemplate<MessageEnd, Event> {
private:
  MessageEnd() {}
  MessageEnd(const MessageEnd &r) {}

  virtual auto type() const -> Type override { return Event::MessageEnd; }
  virtual auto name() const -> const char* override { return "MessageEnd"; }
  virtual auto clone() const -> Event* override { return make(*this); }

  friend class pjs::ObjectTemplate<MessageEnd, Event>;
};

//
// SessionEnd
//

class SessionEnd : public pjs::ObjectTemplate<SessionEnd, Event> {
public:
  enum Error {
    NO_ERROR = 0,
    UNKNOWN_ERROR,
    RUNTIME_ERROR,
    READ_ERROR,
    CANNOT_RESOLVE,
    CONNECTION_REFUSED,
    UNAUTHORIZED,
    BUFFER_OVERFLOW,
  };

  auto error() const -> Error { return m_error; }
  auto message() const -> const std::string& { return m_message; }

private:
  SessionEnd(Error error = NO_ERROR) : m_error(error) {}

  SessionEnd(const std::string &message, Error error = UNKNOWN_ERROR)
    : m_error(error)
    , m_message(message) {}

  SessionEnd(const SessionEnd &r)
    : m_error(r.m_error)
    , m_message(r.m_message) {}

  Error m_error;
  std::string m_message;

  virtual auto type() const -> Type override { return Event::SessionEnd; }
  virtual auto name() const -> const char* override { return "SessionEnd"; }
  virtual auto clone() const -> Event* override { return make(*this); }

  friend class pjs::ObjectTemplate<SessionEnd, Event>;
};

} // namespace pipy

#endif // EVENT_HPP