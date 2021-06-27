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

#include "merge.hpp"
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "listener.hpp"
#include "logging.hpp"

namespace pipy {

//
// Merge
//

Merge::Merge()
{
}

Merge::Merge(pjs::Str *target, pjs::Function *selector)
  : m_session_pool(std::make_shared<SessionPool>())
  , m_target(target)
  , m_selector(selector)
{
}

Merge::Merge(const Merge &r)
  : m_session_pool(r.m_session_pool)
  , m_target(r.m_target)
  , m_selector(r.m_selector)
{
}

Merge::~Merge() {
}

auto Merge::help() -> std::list<std::string> {
  return {
    "merge(target[, selector])",
    "Merges messages from different sessions to a shared pipeline session",
    "target = <string> Name of the pipeline to send messages to",
    "selector = <function> Callback function that gives the name of a session for reuse",
  };
}

void Merge::dump(std::ostream &out) {
  out << "merge";
}

auto Merge::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = true;
  return "merge";
}

auto Merge::clone() -> Filter* {
  return new Merge(*this);
}

void Merge::reset() {
  m_session_pool->free(m_session);
  m_session = nullptr;
  m_head = nullptr;
  m_body = nullptr;
  m_session_end = false;
}

void Merge::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (!m_session) {
    auto mod = pipeline()->module();
    auto pipeline = mod->find_named_pipeline(m_target);
    if (!pipeline) {
      Log::error("[merge] unknown pipeline: %s", m_target->c_str());
      abort();
      return;
    }
    if (m_selector) {
      pjs::Value ret;
      if (!callback(*ctx, m_selector, 0, nullptr, ret)) return;
      auto s = ret.to_string();
      m_session = m_session_pool->alloc(pipeline, s);
      s->release();
    } else {
      m_session = m_session_pool->alloc(pipeline, pjs::Str::empty);
    }
  }

  if (auto e = inp->as<MessageStart>()) {
    m_head = e->head();
    m_body = Data::make();

  } else if (auto data = inp->as<Data>()) {
    if (m_body) m_body->push(*data);

  } else if (inp->is<MessageEnd>()) {
    if (m_body) {
      m_session->input(ctx, m_head, m_body);
      m_head = nullptr;
      m_body = nullptr;
    }
  }

  output(inp);

  if (inp->is<SessionEnd>()) {
    m_session_pool->free(m_session);
    m_session = nullptr;
    m_head = nullptr;
    m_body = nullptr;
    m_session_end = true;
  }
}

void Merge::SessionPool::start_cleaning() {
  if (m_cleaning_scheduled) return;
  m_timer.schedule(1, [this]() {
    m_cleaning_scheduled = false;
    clean();
  });
  m_cleaning_scheduled = true;
}

void Merge::SessionPool::clean() {
  auto p = m_free_sessions.head();
  while (p) {
    auto session = p;
    p = p->next();
    if (++session->m_free_time >= 10) {
      m_free_sessions.remove(session);
      m_sessions.erase(session->m_name);
    }
  }

  if (!m_free_sessions.empty()) {
    start_cleaning();
  }
}

void Merge::SharedSession::input(Context *ctx, pjs::Object *head, Data *body) {
  if (!m_session || m_session->done()) {
    m_session = nullptr;
    m_session = Session::make(ctx, m_pipeline);
  }

  m_session->input(MessageStart::make(head));
  if (body) m_session->input(body);
  m_session->input(MessageEnd::make());
}

} // namespace pipy