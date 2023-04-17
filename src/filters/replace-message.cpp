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

#include "replace-message.hpp"
#include "message.hpp"

namespace pipy {

//
// ReplaceMessage
//

ReplaceMessage::ReplaceMessage(pjs::Object *replacement, const Buffer::Options &options)
  : Replace(replacement)
  , m_body_buffer(options)
{
}

ReplaceMessage::ReplaceMessage(const ReplaceMessage &r)
  : Replace(r)
  , m_body_buffer(r.m_body_buffer)
{
}

ReplaceMessage::~ReplaceMessage()
{
}

void ReplaceMessage::dump(Dump &d) {
  Filter::dump(d);
  d.name = "replaceMessage";
}

auto ReplaceMessage::clone() -> Filter* {
  return new ReplaceMessage(*this);
}

void ReplaceMessage::reset() {
  Replace::reset();
  m_start = nullptr;
  m_body_buffer.clear();
}

void ReplaceMessage::handle(Event *evt) {
  if (!m_start) {
    if (auto start = evt->as<MessageStart>()) {
      m_start = start;
      m_body_buffer.clear();
    } else {
      Replace::pass(evt);
    }

  } else {
    if (auto data = evt->as<Data>()) {
      m_body_buffer.push(*data);

    } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
      pjs::Object *tail = nullptr;
      pjs::Value payload;
      if (auto *end = evt->as<MessageEnd>()) {
        tail = end->tail();
        payload = end->payload();
      }
      pjs::Ref<Message> msg(Message::make(m_start->head(), m_body_buffer.flush(), tail, payload));
      if (!Replace::callback(msg)) return;
      m_start = nullptr;
    }
  }
}

} // namespace pipy
