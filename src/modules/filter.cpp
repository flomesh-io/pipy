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

#include "filter.hpp"
#include "utils.hpp"

NS_BEGIN

auto Filter::help() -> std::list<std::string> {
  return {
    "Let through objects under a path down the pipeline or sends them to a queue",
    "path = Path under which objects are filtered",
    "to = If specified, the name of a queue to send filtered objects",
  };
}

void Filter::config(const std::map<std::string, std::string> &params) {
  m_match = Match(utils::get_param(params, "path"));
  m_queue_name = utils::get_param(params, "to", "");
}

auto Filter::clone() -> Module* {
  auto clone = new Filter();
  clone->m_match = m_match;
  clone->m_queue_name = m_queue_name;
  return clone;
}

void Filter::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>() || obj->is<SessionEnd>()) {
    m_match.reset();
    m_started = false;
    m_queue = nullptr;
    out(std::move(obj));

  } else {
    if (!m_queue_name.empty()) {
      if (!m_queue || ctx->id != m_context_id) {
        m_queue = ctx->get_queue(m_queue_name);
        m_context_id = ctx->id;
      }
    }

    if (obj->is<MessageStart>()) {
      m_match.reset();
      if (m_queue) m_queue->clear();
      out(std::move(obj));

    } else if (obj->is<MessageEnd>()) {
      m_started = false;
      if (m_queue) m_queue->send(clone_object(obj));
      out(std::move(obj));

    } else {
      m_match.process(obj.get());
      if (m_match.matching()) {
        if (!m_started && !obj->is<MapKey>()) {
          m_started = true;
        }
      } else if (m_started) {
        m_started = false;
      }

      if (auto *q = m_queue) {
        if (m_started) q->send(clone_object(obj));
        out(std::move(obj));
      } else if (m_started) {
        out(std::move(obj));
      }
    }
  }
}

NS_END