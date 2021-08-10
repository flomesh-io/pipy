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

#include "replace-event.hpp"

namespace pipy {

ReplaceEvent::ReplaceEvent(Event::Type type)
  : m_type(type)
{
}

ReplaceEvent::ReplaceEvent(Event::Type type, const pjs::Value &replacement)
  : m_type(type)
  , m_replacement(replacement)
{
}

ReplaceEvent::ReplaceEvent(const ReplaceEvent &r)
  : m_type(r.m_type)
  , m_replacement(r.m_replacement)
{
}

ReplaceEvent::~ReplaceEvent()
{
}

auto ReplaceEvent::help() -> std::list<std::string> {
  switch (m_type) {
    case Event::Type::Data:
      return {
        "replaceData([replacement])",
        "Replaces a Data event",
        "replacement = <object|function> Replacement events or a callback function that returns replacement events",
      };
    case Event::Type::MessageStart:
      return {
        "replaceMessageStart([replacement])",
        "Replaces a MessageStart event",
        "replacement = <object|function> Replacement events or a callback function that returns replacement events",
      };
    case Event::Type::MessageEnd:
      return {
        "replaceMessageEnd([replacement])",
        "Replaces a MessageEnd event",
        "replacement = <object|function> Replacement events or a callback function that returns replacement events",
      };
    case Event::Type::SessionEnd:
      return {
        "replaceSessionEnd([replacement])",
        "Replaces a SessionEnd event",
        "replacement = <object|function> Replacement events or a callback function that returns replacement events",
      };
    default: return std::list<std::string>();
  }
}

void ReplaceEvent::dump(std::ostream &out) {
  switch (m_type) {
    case Event::Type::Data: out << "replaceData"; break;
    case Event::Type::MessageStart: out << "replaceMessageStart"; break;
    case Event::Type::MessageEnd: out << "replaceMessageEnd"; break;
    case Event::Type::SessionEnd: out << "replaceSessionEnd"; break;
    default: break;
  }
}

auto ReplaceEvent::clone() -> Filter* {
  return new ReplaceEvent(*this);
}

void ReplaceEvent::reset() {
}

void ReplaceEvent::process(Context *ctx, Event *inp) {
  if (inp->type() == m_type) {
    pjs::Object *mctx = m_type == Event::MessageStart ? inp->as<MessageStart>()->context() : nullptr;
    if (m_replacement.is_function()) {
      pjs::Value arg(inp), result;
      if (callback(*ctx, m_replacement.f(), 1, &arg, result)) {
        output(result, mctx);
      }
    } else {
      output(m_replacement, mctx);
    }
  } else {
    output(inp);
  }
}

} // namespace pipy