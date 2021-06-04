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

#include "replace-body.hpp"

namespace pipy {

//
// ReplaceBody
//

ReplaceBody::ReplaceBody()
{
}

ReplaceBody::ReplaceBody(const pjs::Value &replacement)
  : m_replacement(replacement)
{
}

ReplaceBody::ReplaceBody(const ReplaceBody &r)
  : ReplaceBody(r.m_replacement)
{
}

ReplaceBody::~ReplaceBody()
{
}

auto ReplaceBody::help() -> std::list<std::string> {
  return {
    "replaceMessageBody([replacement])",
    "Replaces an entire message body",
    "callback = <object|function> Replacement events or a callback function that returns replacement events",
  };
}

void ReplaceBody::dump(std::ostream &out) {
  out << "replaceMessageBody";
}

auto ReplaceBody::clone() -> Filter* {
  return new ReplaceBody(*this);
}

void ReplaceBody::reset() {
  m_body = nullptr;
}

void ReplaceBody::process(Context *ctx, Event *inp) {
  if (inp->is<MessageStart>()) {
    m_body = Data::make();

  } else if (auto data = inp->as<Data>()) {
    if (m_body) {
      m_body->push(*data);
      return;
    }

  } else if (inp->is<MessageEnd>()) {
    if (m_replacement.is_function()) {
      pjs::Value arg(m_body), result;
      if (callback(*ctx, m_replacement.f(), 1, &arg, result)) {
        output(result);
      }
    } else {
      output(m_replacement);
    }
    m_body = nullptr;
  }

  output(inp);
}

} // namespace pipy