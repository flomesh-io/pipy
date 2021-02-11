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

#include "script.hpp"
#include "js.hpp"
#include "utils.hpp"

#include <fstream>
#include <sstream>

NS_BEGIN

//
// Script
//

Script::Script() {
}

Script::~Script() {
  delete m_session;
}

auto Script::help() -> std::list<std::string> {
  return {
    "Invokes a stream handler written in JavaScript",
    "source = Filename of the JavaScript module",
  };
}

void Script::config(const std::map<std::string, std::string> &params) {
  auto source = utils::get_param(params, "source");
  m_program = new js::Program(source);
}

auto Script::clone() -> Module* {
  auto clone = new Script();
  clone->m_program = m_program;
  clone->m_session = m_program ? m_program->run() : nullptr;
  return clone;
}

void Script::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  m_session->process(ctx, std::move(obj), out);
}

NS_END
