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

#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include "data.hpp"
#include "list.hpp"

#include <functional>

namespace pipy {

class MessageStart;
class MessageEnd;
class MessageBuffer;

//
// Message
//

class Message :
  public pjs::ObjectTemplate<Message>,
  public List<Message>::Item
{
public:
  static bool is_events(pjs::Object *obj);
  static bool to_events(pjs::Object *obj, const std::function<bool(Event*)> &cb);
  static bool to_events(const pjs::Value &value, const std::function<bool(Event*)> &cb);
  static auto from(MessageStart *start, Data *body, MessageEnd *end) -> Message*;
  static bool output(const pjs::Value &events, EventTarget::Input *input);
  static bool output(pjs::Object *events, EventTarget::Input *input);

  auto head() const -> pjs::Object* { return m_head; }
  auto tail() const -> pjs::Object* { return m_tail; }
  auto body() const -> Data* { return m_body; }
  auto payload() const -> const pjs::Value& { return m_payload; }

  auto clone() const -> Message* {
    return Message::make(m_head, m_body, m_tail, m_payload);
  }

  void write(EventTarget::Input *input);

private:
  Message() {}

  Message(Data *body)
    : m_body(body) {}

  Message(const std::string &body)
    : m_body(s_dp.make(body)) {}

  Message(pjs::Object *head, Data *body)
    : m_head(head)
    , m_body(body) {}

  Message(pjs::Object *head, const std::string &body)
    : m_head(head)
    , m_body(s_dp.make(body)) {}

  Message(pjs::Object *head, Data *body, pjs::Object *tail)
    : m_head(head)
    , m_tail(tail)
    , m_body(body) {}

  Message(pjs::Object *head, Data *body, pjs::Object *tail, const pjs::Value &payload)
    : m_head(head)
    , m_tail(tail)
    , m_body(body)
    , m_payload(payload) {}

  Message(pjs::Object *head, const std::string &body, pjs::Object *tail)
    : m_head(head)
    , m_tail(tail)
    , m_body(s_dp.make(body)) {}

  Message(pjs::Object *head, const std::string &body, pjs::Object *tail, const pjs::Value &payload)
    : m_head(head)
    , m_tail(tail)
    , m_body(s_dp.make(body))
    , m_payload(payload) {}

  ~Message() {}

  pjs::Ref<pjs::Object> m_head;
  pjs::Ref<pjs::Object> m_tail;
  pjs::Ref<Data> m_body;
  pjs::Value m_payload;
  bool m_in_buffer = false;

  static Data::Producer s_dp;

  friend class pjs::ObjectTemplate<Message>;
  friend class MessageBuffer;
};

//
// MessageBuffer
//

class MessageBuffer {
public:
  bool empty() const {
    return m_messages.empty();
  }

  void push(Message *m) {
    if (m->m_in_buffer) m = m->clone();
    m->m_in_buffer = true;
    m->retain();
    m_messages.push(m);
  }

  auto shift() -> Message* {
    if (m_messages.empty()) return nullptr;
    auto m = m_messages.head();
    m_messages.remove(m);
    m->m_in_buffer = false;
    return m;
  }

  void unshift(Message *m) {
    if (m->m_in_buffer) m = m->clone();
    m->m_in_buffer = true;
    m->retain();
    m_messages.unshift(m);
  }

  void iterate(const std::function<void(Message*)> &cb) {
    for (auto m = m_messages.head(); m; m = m->next()) {
      cb(m);
    }
  }

  void flush(const std::function<void(Message*)> &out) {
    List<Message> messages(std::move(m_messages));
    while (auto m = messages.head()) {
      messages.remove(m);
      m->m_in_buffer = false;
      out(m);
      m->release();
    }
  }

  void clear() {
    List<Message> messages(std::move(m_messages));
    while (auto m = messages.head()) {
      messages.remove(m);
      m->m_in_buffer = false;
      m->release();
    }
  }

private:
  List<Message> m_messages;
};

//
// MessageReader
//

class MessageReader {
public:
  void reset();
  auto read(Event *evt) -> Message*;

private:
  pjs::Ref<MessageStart> m_start;
  Data m_buffer;
};

} // namespace pipy

#endif // MESSAGE_HPP
