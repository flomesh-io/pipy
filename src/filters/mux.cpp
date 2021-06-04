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

#include "mux.hpp"
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "listener.hpp"
#include "logging.hpp"

namespace pipy {

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(pjs::Str *target, pjs::Function *selector)
  : m_session_pool(std::make_shared<SessionPool>())
  , m_target(target)
  , m_selector(selector)
{
}

Mux::Mux(const Mux &r)
  : m_session_pool(r.m_session_pool)
  , m_target(r.m_target)
  , m_selector(r.m_selector)
{
}

Mux::~Mux() {
}

auto Mux::help() -> std::list<std::string> {
  return {
    "mux(target[, selector])",
    "Sends messages from different sessions to a shared pipeline session",
    "target = <string> Name of the pipeline to send messages to",
    "selector = <function> Callback function that gives the name of a session for reuse",
  };
}

void Mux::dump(std::ostream &out) {
  out << "mux";
}

auto Mux::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = false;
  return "mux";
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

void Mux::reset() {
  while (!m_queue.empty()) {
    m_queue.front()->on_output = nullptr;
    m_queue.pop();
  }
  m_session_pool->free(m_session);
  m_session = nullptr;
  m_head = nullptr;
  m_body = nullptr;
  m_session_end = false;
}

void Mux::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (!m_session) {
    auto mod = pipeline()->module();
    auto pipeline = mod->find_named_pipeline(m_target);
    if (!pipeline) {
      Log::error("[mux] unknown pipeline: %s", m_target->c_str());
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
      auto channel = new Channel;
      channel->on_output = [=](Event *inp) {
        if (m_queue.empty()) return;
        if (inp->is<MessageEnd>() || inp->is<SessionEnd>()) {
          m_queue.pop();
        }
        output(inp);
      };
      m_queue.push(channel);
      m_session->input(channel, ctx, m_head, m_body);
      m_head = nullptr;
      m_body = nullptr;
    }

  } else if (inp->is<SessionEnd>()) {
    m_session_pool->free(m_session);
    m_session = nullptr;
    m_head = nullptr;
    m_body = nullptr;
    m_session_end = true;
  }
}

void Mux::SessionPool::start_cleaning() {
  if (m_cleaning_scheduled) return;
  m_timer.schedule(1, [this]() {
    m_cleaning_scheduled = false;
    clean();
  });
  m_cleaning_scheduled = true;
}

void Mux::SessionPool::clean() {
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

void Mux::SharedSession::input(Channel *channel, Context *ctx, pjs::Object *head, Data *body) {
  if (!m_session || m_session->done()) {
    m_session = nullptr;
    m_session = Session::make(ctx, m_pipeline);
    m_session->on_output([this](Event *inp) {
      if (m_queue.empty()) return;
      if (inp->is<SessionEnd>()) {
        while (!m_queue.empty()) {
          auto &output = m_queue.front()->on_output;
          if (output) output(inp);
          m_queue.pop();
        }
      } else {
        auto channel = m_queue.front().get();
        auto &output = channel->on_output;
        if (output) output(inp);
        if (inp->is<MessageEnd>()) m_queue.pop();
      }
    });
  }

  if (m_queue.size() >= m_buffer_limit) {
    pjs::Ref<SessionEnd> end(SessionEnd::make(SessionEnd::BUFFER_OVERFLOW));
    while (!m_queue.empty()) {
      auto &output = m_queue.front()->on_output;
      if (output) output(end);
      m_queue.pop();
    }
    Log::error("[mux] buffer overflow");
  }

  m_queue.push(std::unique_ptr<Channel>(channel));
  m_session->input(MessageStart::make(head));
  if (body) m_session->input(body);
  m_session->input(MessageEnd::make());
}

} // namespace pipy