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

namespace pipy {

//
// Message
//

class Message : public pjs::ObjectTemplate<Message> {
public:
  static bool output(const pjs::Value &evt, EventTarget::Input *input);

  auto head() const -> pjs::Object* { return m_head; }
  auto tail() const -> pjs::Object* { return m_tail; }
  auto body() const -> Data* { return m_body; }
  auto payload() const -> const pjs::Value& { return m_payload; }

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

  thread_local static Data::Producer s_dp;

  friend class pjs::ObjectTemplate<Message>;
};

} // namespace pipy

#endif // MESSAGE_HPP