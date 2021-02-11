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

#include "hello.hpp"
#include "utils.hpp"

NS_BEGIN

//
// Hello
//

Hello::Hello() {
}

Hello::~Hello() {
}

auto Hello::help() -> std::list<std::string> {
  return {
    "Outputs a text message on reception of an input message",
    "message = Content of message as a string",
  };
}

void Hello::config(const std::map<std::string, std::string> &params) {
  m_message = utils::unescape(utils::get_param(params, "message", "Hello!\n"));
}

auto Hello::clone() -> Module* {
  auto clone = new Hello();
  clone->m_message = m_message;
  return clone;
}

void Hello::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>() || obj->is<SessionEnd>()) {
    out(std::move(obj));

  } else if (obj->is<MessageEnd>()) {
    auto msg = ctx->evaluate(m_message);
    out(make_object<MessageStart>());
    out(make_object<Data>(msg));
    out(make_object<MessageEnd>());
  }
}

NS_END
