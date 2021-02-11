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

#include "erase.hpp"
#include "utils.hpp"

NS_BEGIN

auto Erase::help() -> std::list<std::string> {
  return {
    "Removes objects under a path and lets the rest through down the pipeline",
    "path = Path under which objects are removed",
  };
}

void Erase::config(const std::map<std::string, std::string> &params) {
  m_match = Match(utils::get_param(params, "path"));
}

auto Erase::clone() -> Module* {
  auto clone = new Erase();
  clone->m_match = m_match;
  return clone;
}

void Erase::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>() || obj->is<SessionEnd>() ||
      obj->is<MessageStart>() || obj->is<MessageEnd>())
  {
    m_match.reset();
    out(std::move(obj));

  } else {
    m_match.process(obj.get());
    if (!m_match.matching()) {
      out(std::move(obj));
    }
  }
}

NS_END