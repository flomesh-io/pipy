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

OnMessage::OnMessage()
{
}

OnMessage::OnMessage(pjs::Function *callback)
  : m_callback(callback)
{
}

OnMessage::OnMessage(const OnMessage &r)
  : OnMessage(r.m_callback)
{
}

OnMessage::~OnMessage()
{
}

auto OnMessage::help() -> std::list<std::string> {
  return {
    "onMessage(callback)",
    "Handles a complete message including the head and the body",
    "callback = <function> Callback function that receives a complete message",
  };
}

void OnMessage::dump(std::ostream &out) {
  out << "onMessage";
}

auto OnMessage::clone() -> Filter* {
  return new OnMessage(*this);
}

void OnMessage::reset() {
  m_mctx = nullptr;
  m_head = nullptr;
  m_body = nullptr;
}

void OnMessage::process(Context *ctx, Event *inp) {
  if (auto e = inp->as<MessageStart>()) {
    m_mctx = e->context();
    m_head = e->head();
    m_body = Data::make();

  } else if (auto *data = inp->as<Data>()) {
    if (m_body) m_body->push(*data);

  } else if (inp->is<MessageEnd>()) {
    if (m_body) {
      pjs::Value arg(Message::make(m_mctx, m_head, m_body)), result;
      if (!callback(*ctx, m_callback, 1, &arg, result)) return;
      m_mctx = nullptr;
      m_head = nullptr;
      m_body = nullptr;
    }
  }

  output(inp);
}

} // namespace pipy