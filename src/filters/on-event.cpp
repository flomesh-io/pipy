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

#include "on-event.hpp"

namespace pipy {

OnEvent::OnEvent(Event::Type type)
  : m_type(type)
{
}

OnEvent::OnEvent(Event::Type type, pjs::Function *callback)
  : m_type(type)
  , m_callback(callback)
{
}

OnEvent::OnEvent(const OnEvent &r)
  : m_type(r.m_type)
  , m_callback(r.m_callback)
{
}

OnEvent::~OnEvent()
{
}

auto OnEvent::help() -> std::list<std::string> {
  switch (m_type) {
    case Event::Type::Data:
      return {
        "onData(callback)",
        "Handles a Data event",
        "callback = <function> Callback function that receives a Data event",
      };
    case Event::Type::MessageStart:
      return {
        "onMessageStart(callback)",
        "Handles a MessageStart event",
        "callback = <function> Callback function that receives a MessageStart event",
      };
    case Event::Type::MessageEnd:
      return {
        "onMessageEnd(callback)",
        "Handles a MessageEnd event",
        "callback = <function> Callback function that receives a MessageEnd event",
      };
    case Event::Type::SessionEnd:
      return {
        "onSessionEnd(callback)",
        "Handles a SessionEnd event",
        "callback = <function> Callback function that receives a SessionEnd event",
      };
    default: return std::list<std::string>();
  }
}

void OnEvent::dump(std::ostream &out) {
  switch (m_type) {
    case Event::Type::Data: out << "onData"; break;
    case Event::Type::MessageStart: out << "onMessageStart"; break;
    case Event::Type::MessageEnd: out << "onMessageEnd"; break;
    case Event::Type::SessionEnd: out << "onSessionEnd"; break;
    default: break;
  }
}

auto OnEvent::clone() -> Filter* {
  return new OnEvent(*this);
}

void OnEvent::reset() {
}

void OnEvent::process(Context *ctx, Event *inp) {
  if (inp->type() == m_type) {
    pjs::Value arg(inp), result;
    if (!callback(*ctx, m_callback, 1, &arg, result)) return;
  }

  output(inp);
}

} // namespace pipy