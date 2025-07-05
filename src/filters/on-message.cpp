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

#include "on-message.hpp"
#include "message.hpp"

namespace pipy {

//
// OnMessage
//

OnMessage::OnMessage(pjs::Function *callback, bool one, const DataBuffer::Options &options)
  : Handle(callback)
  , m_body_buffer(options, Filter::buffer_stats())
  , m_one(one)
{
}

OnMessage::OnMessage(const OnMessage &r)
  : Handle(r)
  , m_body_buffer(r.m_body_buffer)
  , m_one(r.m_one)
{
}

OnMessage::~OnMessage()
{
}

void OnMessage::dump(Dump &d) {
  Filter::dump(d);
  d.name = m_one ? "handleOneMessage" : "handleMessage";
}

auto OnMessage::clone() -> Filter* {
  return new OnMessage(*this);
}

void OnMessage::reset() {
  Handle::reset();
  m_start = nullptr;
  m_body_buffer.clear();
  m_ended = false;
}

void OnMessage::handle(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    m_start = start;
    m_body_buffer.clear();

  } else if (auto *data = evt->as<Data>()) {
    if (m_start) {
      m_body_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    auto end = evt->as<MessageEnd>();
    if (m_start) {
      if (!m_one || !m_ended) {
        pjs::Object *tail = nullptr;
        pjs::Value payload;
        if (end) {
          tail = end->tail();
          payload = end->payload();
        }
        auto body = m_body_buffer.flush();
        pjs::Ref<Message> msg(Message::make(m_start->head(), body, tail, payload)), result;
        m_start = nullptr;
        m_ended = true;
        if (Handle::callback(msg)) {
          Handle::defer(evt);
        }
        return;
      }
    } else if (!end && m_one && !m_ended) {
      m_ended = true;
      if (Handle::callback(evt->as<StreamEnd>())) {
        Handle::defer(evt);
      }
      return;
    }
  }

  Handle::pass(evt);
}

} // namespace pipy
