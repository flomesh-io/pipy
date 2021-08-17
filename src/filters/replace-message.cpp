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
#include "logging.hpp"

namespace pipy {

//
// ReplaceMessage
//

ReplaceMessage::ReplaceMessage()
{
}

ReplaceMessage::ReplaceMessage(const pjs::Value &replacement, int size_limit)
  : m_replacement(replacement)
  , m_size_limit(size_limit)
{
}

ReplaceMessage::ReplaceMessage(const ReplaceMessage &r)
  : ReplaceMessage(r.m_replacement, r.m_size_limit)
{
}

ReplaceMessage::~ReplaceMessage()
{
}

auto ReplaceMessage::help() -> std::list<std::string> {
  return {
    "replaceMessage([sizeLimit, ]replacement)",
    "Replaces a complete message including the head and the body",
    "sizeLimit = <number|string> Maximum number of bytes to collect from the message body",
    "callback = <object|function> Replacement events or a callback function that returns replacement events",
  };
}

void ReplaceMessage::dump(std::ostream &out) {
  out << "replaceMessage";
}

auto ReplaceMessage::clone() -> Filter* {
  return new ReplaceMessage(*this);
}

void ReplaceMessage::reset() {
  m_mctx = nullptr;
  m_head = nullptr;
  m_body = nullptr;
  m_discarded_size = 0;
}

void ReplaceMessage::process(Context *ctx, Event *inp) {
  if (auto e = inp->as<MessageStart>()) {
    m_mctx = e->context();
    m_head = e->head();
    m_body = Data::make();
    return;

  } else if (auto data = inp->as<Data>()) {
    if (m_body && data->size() > 0) {
      if (m_size_limit >= 0) {
        auto room = m_size_limit - m_body->size();
        if (room >= data->size()) {
          m_body->push(*data);
        } else if (room > 0) {
          Data buf(*data);
          auto discard = buf.size() - room;
          buf.pop(discard);
          m_body->push(buf);
          m_discarded_size += discard;
        } else {
          m_discarded_size += data->size();
        }
      } else {
        m_body->push(*data);
      }
      return;
    }

  } else if (inp->is<MessageEnd>()) {
    if (m_body) {
      if (m_discarded_size > 0) {
        Log::error(
          "[replaceMessage] %d bytes were discarded due to buffer size limit of %d",
          m_discarded_size, m_size_limit
        );
      }
      if (m_replacement.is_function()) {
        pjs::Value arg(Message::make(m_mctx, m_head, m_body)), result;
        if (callback(*ctx, m_replacement.f(), 1, &arg, result)) {
          output(result, m_mctx);
        }
      } else {
        output(m_replacement, m_mctx);
      }
      m_mctx = nullptr;
      m_head = nullptr;
      m_body = nullptr;
      m_discarded_size = 0;
      return;
    }
  }

  output(inp);
}

} // namespace pipy