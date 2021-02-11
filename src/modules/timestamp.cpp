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

#include "timestamp.hpp"
#include "utils.hpp"

#include <chrono>

NS_BEGIN

//
// Timestamp
//

Timestamp::Timestamp() {
}

Timestamp::~Timestamp() {
}

auto Timestamp::help() -> std::list<std::string> {
  return {
    "Timestamps in a context variable the occurrences of an object type",
    "when = Type of object to timestamp, options including SessionStart, SessionEnd, MessageStart, MessageEnd",
    "variable = Name of the context variable where a timestamp is saved",
  };
}

void Timestamp::config(const std::map<std::string, std::string> &params) {
  auto when = utils::get_param(params, "when");
  if (when == "SessionStart") m_when = Object::SessionStart; else
  if (when == "SessionEnd") m_when = Object::SessionEnd; else
  if (when == "MessageStart") m_when = Object::MessageStart; else
  if (when == "MessageEnd") m_when = Object::MessageEnd; else {
    std::string msg("invalid value for parameter when: ");
    throw std::runtime_error(msg + when);
  }

  m_variable = utils::get_param(params, "variable");
}

auto Timestamp::clone() -> Module* {
  auto clone = new Timestamp();
  clone->m_when = m_when;
  clone->m_variable = m_variable;
  return clone;
}

void Timestamp::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->type() == m_when) {
    auto t = std::chrono::system_clock::now().time_since_epoch();
    auto n = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
    ctx->variables[m_variable] = std::to_string(n);
  }

  out(std::move(obj));
}

NS_END
