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

OnMessage::OnMessage(pjs::Function *callback, const DataBuffer::Options &options)
  : Handle(callback)
  , m_body_buffer(options, Filter::buffer_stats())
{
}

OnMessage::OnMessage(const OnMessage &r)
  : Handle(r)
  , m_body_buffer(r.m_body_buffer)
{
}

OnMessage::~OnMessage()
{
}

void OnMessage::dump(Dump &d) {
  Filter::dump(d);
  d.name = "handleMessage";
}

auto OnMessage::clone() -> Filter* {
  return new OnMessage(*this);
}

void OnMessage::reset() {
  Handle::reset();
  m_start = nullptr;
  m_body_buffer.clear();
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
    if (m_start) {
      pjs::Object *tail = nullptr;
      pjs::Value payload;
      if (auto *end = evt->as<MessageEnd>()) {
        tail = end->tail();
        payload = end->payload();
      }
      auto body = m_body_buffer.flush();
      pjs::Ref<Message> msg(Message::make(m_start->head(), body, tail, payload)), result;
      if (Handle::callback(msg)) {
        Handle::defer(evt);
      }
      m_start = nullptr;
      return;
    }
  }

  Handle::pass(evt);
}

} // namespace pipy
