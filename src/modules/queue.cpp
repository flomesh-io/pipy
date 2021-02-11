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

#include "queue.hpp"
#include "utils.hpp"

NS_BEGIN

//
// Queue
//

auto Queue::help() -> std::list<std::string> {
  return {
    "Sends everything from the stream to a queue",
    "to = Name of the target queue in the current context",
  };
}

void Queue::config(const std::map<std::string, std::string> &params) {
  m_queue_name = utils::get_param(params, "to");
}

auto Queue::clone() -> Module* {
  auto clone = new Queue();
  clone->m_queue_name = m_queue_name;
  return clone;
}

void Queue::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>() || obj->is<SessionEnd>()) {
    m_queue = nullptr;

  } else {
    if (!m_queue || ctx->id != m_context_id) {
      m_queue = ctx->get_queue(m_queue_name);
      m_context_id = ctx->id;
    }

    if (auto *q = m_queue) q->send(clone_object(obj));
  }

  out(std::move(obj));
}

NS_END
