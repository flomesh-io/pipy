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
#include "data.hpp"

#include <limits>

namespace pipy {

//
// Muxer::Options
//

Muxer::Options::Options(pjs::Object *options) {
  thread_local static pjs::ConstStr s_max_idle("maxIdle");
  thread_local static pjs::ConstStr s_max_sessions("maxSessions");
  Value(options, s_max_idle)
    .get(max_idle)
    .check_nullable();
  Value(options, s_max_sessions)
    .get(max_sessions)
    .check_nullable();
}

//
// Muxer
//

auto Muxer::alloc(Filter *filter, const pjs::Value &key) -> Session* {
  auto weak_ptr = (key.is_object() && key.o() ? key.o()->weak_ptr() : nullptr);
  if (!weak_ptr) {
    auto i = m_pools.find(key);
    if (i != m_pools.end()) {
      return i->second->alloc(filter);
    }
  } else {
    auto i = m_weak_pools.find(weak_ptr);
    if (i != m_weak_pools.end()) {
      return i->second->alloc(filter);
    }
  }

  if (weak_ptr) {
    auto pool = new SessionPool(this, weak_ptr);
    m_weak_pools[weak_ptr] = pool;
    return pool->alloc(filter);
  } else {
    auto pool = new SessionPool(this, key);
    m_pools[key] = pool;
    return pool->alloc(filter);
  }
}

void Muxer::shutdown() {
  m_has_shutdown = true;
}

void Muxer::schedule_recycling() {
  if (m_has_recycling_scheduled) return;
  if (m_recycle_pools.empty()) return;

  m_recycle_timer.schedule(
    1.0,
    [this]() {
      InputContext ic;
      m_has_recycling_scheduled = false;
      auto now = m_has_shutdown ? std::numeric_limits<double>::infinity() : utils::now();
      auto p = m_recycle_pools.head();
      while (p) {
        auto pool = p; p = p->next();
        pool->recycle(now);
      }
      schedule_recycling();
      release();
    }
  );

  retain();
  m_has_recycling_scheduled = true;
}

//
// Muxer::SessionPool
//

auto Muxer::SessionPool::alloc(Filter *filter) -> Session* {
  if (auto s = m_sessions.head()) {
    if (s->is_idle()) {
      return s;
    }
  }

  auto max_sessions = m_muxer->m_options.max_sessions;
  if (max_sessions > 0 && m_sessions.size() >= max_sessions) {
    for (auto s = m_sessions.head(); s; s = s->next()) {
      if (s->m_allow_queuing) {
        return s;
      }
    }
  }

  auto s = m_muxer->on_muxer_session_open(filter);
  s->m_pool = this;
  m_sessions.unshift(s);
  schedule_recycling();
  return s;
}

void Muxer::SessionPool::recycle(double now) {
  for (auto s = m_aborted_sessions.head(); s; ) {
    auto session = s; s = s->next();
    m_muxer->on_muxer_session_close(session);
  }
  m_aborted_sessions.clear();

  auto max_idle = m_muxer->m_options.max_idle * 1000;
  if (max_idle < 0) {
    max_idle = std::numeric_limits<double>::infinity();
  } else {
    max_idle *= 1000;
  }

  auto s = m_sessions.head();
  while (s) {
    auto session = s; s = s->next();
    if (session->m_streams.size() > 0) break;
    if (session->is_idle_timeout(now, max_idle) || m_weak_ptr_gone) {
      session->m_pool = nullptr;
      m_sessions.remove(session);
      m_muxer->on_muxer_session_close(session);
    }
  }

  if (m_sessions.empty()) {
    if (m_weak_key) {
      m_muxer->m_weak_pools.erase(m_weak_key);
    } else {
      m_muxer->m_pools.erase(m_key);
    }
    if (m_has_recycling_scheduled) {
      m_muxer->m_recycle_pools.remove(this);
    }
    delete this;
  }
}

void Muxer::SessionPool::sort(Session *session) {
  if (session) {
    if (session->m_has_aborted) {
      session->m_pool = nullptr;
      m_sessions.remove(session);
      m_aborted_sessions.push(session);
    } else {
      auto p = session->back();
      while (p && p->m_streams.size() > session->m_streams.size()) p = p->back();
      if (p == session->back()) {
        auto p = session->next();
        while (p && p->m_streams.size() < session->m_streams.size()) p = p->next();
        if (p != session->next()) {
          m_sessions.remove(session);
          if (p) {
            m_sessions.insert(session, p);
          } else {
            m_sessions.push(session);
          }
        }
      } else {
        m_sessions.remove(session);
        if (p) {
          m_sessions.insert(session, p->next());
        } else {
          m_sessions.unshift(session);
        }
      }
    }
  }

  schedule_recycling();
}

void Muxer::SessionPool::schedule_recycling() {
  auto s = m_sessions.head();
  if (s && s->m_streams.size() > 0 && m_aborted_sessions.empty()) {
    if (m_has_recycling_scheduled) {
      m_muxer->m_recycle_pools.remove(this);
      m_has_recycling_scheduled = false;
    }
  } else {
    if (!m_has_recycling_scheduled) {
      m_muxer->m_recycle_pools.push(this);
      m_muxer->schedule_recycling();
      m_has_recycling_scheduled = true;
    }
  }
}

void Muxer::SessionPool::on_weak_ptr_gone() {
  m_muxer->m_weak_pools.erase(m_weak_key);
  m_weak_ptr_gone = true;
  schedule_recycling();
}

//
// Mux::Options
//

Mux::Options::Options(pjs::Object *options)
  : Muxer::Options(options)
{
  Value(options, "messageKey")
    .get(message_key_f)
    .check_nullable();
}

//
// Mux
//

Mux::Mux(pjs::Function *session_selector)
  : m_pool(new Pool)
  , m_session_selector(session_selector)
{
}

Mux::Mux(pjs::Function *session_selector, const Options &options)
  : m_pool(new Pool(options))
  , m_session_selector(session_selector)
  , m_options(options)
{
}

Mux::Mux(const Mux &r)
  : Filter(r)
  , m_pool(r.m_pool)
  , m_session_selector(r.m_session_selector)
  , m_options(r.m_options)
{
}

void Mux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "mux";
  d.sub_type = Dump::MUX;
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

void Mux::reset() {
  Filter::reset();
  if (m_request) {
    m_request->discard();
    m_request = nullptr;
  }
  m_queue = nullptr;
  m_has_error = false;
}

void Mux::shutdown() {
  Filter::shutdown();
  m_pool->shutdown();
}

void Mux::process(Event *evt) {
  if (!m_has_error) {
    if (auto start = evt->as<MessageStart>()) {
      pjs::Value msg_key;
      if (auto f = m_options.message_key_f.get()) {
        pjs::Value msg(start);
        if (!Filter::callback(f, 1, &msg, msg_key)) {
          m_has_error = true;
          return;
        }
      }
      if (!m_request) {
        pjs::Value key;
        if (m_session_selector) {
          if (!Filter::eval(m_session_selector, key)) {
            m_has_error = true;
            return;
          }
        }
        m_queue = static_cast<Queue*>(m_pool->alloc(this, key));
        m_request = m_queue->alloc(Filter::output(), msg_key);
      }
    }

    if (m_request) {
      m_request->input()->input(evt);
    }
  }
}

//
// Mux::Request
//

void Mux::Request::discard() {
  EventFunction::chain(nullptr);
}

void Mux::Request::on_event(Event *evt) {
  if (auto s = Muxer::Stream::session()) {
    auto queue = static_cast<Queue*>(s);
    auto input = queue->m_pipeline->input();
    auto is_sending = m_is_sending;

    if (evt->is<MessageStart>()) {
      if (!m_started) {
        m_started = true;
        if (is_sending) {
          input->input(evt);
        } else {
          m_buffer.push(evt);
        }
      }

    } else if (evt->is<Data>()) {
      if (m_started && !m_ended) {
        if (is_sending) {
          input->input(evt);
        } else {
          m_buffer.push(evt);
        }
      }

    } else if (evt->is_end()) {
      if (m_started && !m_ended) {
        m_ended = true;
        if (evt->is<StreamEnd>()) {
          evt = MessageEnd::make();
        }
        if (is_sending) {
          input->input(evt);
          auto s = Muxer::Stream::next();
          while (s) {
            auto request = static_cast<Request*>(s);
            request->m_is_sending = true;
            request->m_buffer.flush([&](Event *evt) { input->input(evt); });
            if (!request->m_ended) break;
            s = s->next();
            if (request->m_key.is_nullish()) {
              queue->free(request);
            }
          }
        } else {
          m_buffer.push(evt);
        }
      }
    }
  }
}

//
// Mux::Queue
//

Mux::Queue::Queue(Mux *mux)
  : m_message_key_f(mux->m_options.message_key_f)
{
  allow_queuing(true);
  auto p = mux->sub_pipeline(0, true, EventTarget::input());
  m_pipeline = p;
  m_context = p->context();
  p->on_eos([this](StreamEnd *) { Muxer::Session::abort(); });
  p->start();
}

auto Mux::Queue::alloc(EventTarget::Input *output, const pjs::Value &key) -> Request* {
  auto r = new Request(key);
  r->chain(output);
  Muxer::Session::append(r);
  if (auto back = r->back()) {
    auto last = static_cast<Request*>(back);
    if (last->m_is_sending && last->m_ended) {
      r->m_is_sending = true;
    }
  } else {
    r->m_is_sending = true;
  }
  return r->retain();
}

void Mux::Queue::free(Request *r) {
  r->discard();
  Muxer::Session::remove(r);
  r->release();
}

void Mux::Queue::free_all() {
  for (auto r = Muxer::Session::head(); r; ) {
    auto request = static_cast<Request*>(r); r = r->next();
    free(request);
  }
}

void Mux::Queue::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_started) {
      m_started = true;
      if (auto f = m_message_key_f.get()) {
        pjs::Value msg(start), key;
        (*f)(*m_context, 1, &msg, key);
        if (!m_context->ok()) m_context->reset();
        if (!key.is_nullish()) {
          for (auto r = Muxer::Session::head(); r; r = r->next()) {
            auto request = static_cast<Request*>(r);
            if (pjs::Value::is_identical(request->m_key, key)) {
              m_current_request = request;
              break;
            }
          }
        }
      }
      if (m_current_request) {
        m_current_request->output()->input(evt);
      }
    }

  } else if (evt->is<Data>()) {
    if (m_started) {
      if (m_current_request) {
        m_current_request->output()->input(evt);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_started) {
      m_started = false;
      if (m_current_request) {
        m_current_request->output()->input(evt);
        free(m_current_request);
        m_current_request = nullptr;
      }
    }

  } else if (evt->is<StreamEnd>()) {
    for (auto r = Muxer::Session::head(); r; r = r->next()) {
      static_cast<Request*>(r)->output()->input(evt);
    }
    free_all();
  }
}

//
// Mux::Pool
//

Mux::Pool::Pool()
{
}

Mux::Pool::Pool(const Options &options)
  : Muxer(options)
  , m_options(options)
{
}

auto Mux::Pool::on_muxer_session_open(Filter *filter) -> Session* {
  auto s = new Queue(static_cast<Mux*>(filter));
  return s->retain();
}

void Mux::Pool::on_muxer_session_close(Session *session) {
  auto s = static_cast<Queue*>(session);
  s->free_all();
  s->release();
}

//
// MuxQueue::Options
//

MuxQueue::Options::Options(pjs::Object *options)
  : Muxer::Options(options)
{
  Value(options, "blocking")
    .get(blocking)
    .check_nullable();
}

//
// MuxQueue
//

MuxQueue::MuxQueue(pjs::Function *session_selector)
  : m_pool(new Pool)
  , m_session_selector(session_selector)
{
}

MuxQueue::MuxQueue(pjs::Function *session_selector, const Options &options)
  : m_pool(new Pool(options))
  , m_session_selector(session_selector)
  , m_options(options)
{
}

MuxQueue::MuxQueue(const MuxQueue &r)
  : Filter(r)
  , m_pool(r.m_pool)
  , m_session_selector(r.m_session_selector)
  , m_options(r.m_options)
{
}

void MuxQueue::dump(Dump &d) {
  Filter::dump(d);
  d.name = "muxQueue";
  d.sub_type = Dump::MUX;
}

auto MuxQueue::clone() -> Filter* {
  return new MuxQueue(*this);
}

void MuxQueue::reset() {
  Filter::reset();
  if (m_request) {
    m_request->discard();
    m_request = nullptr;
  }
  m_queue = nullptr;
  m_has_error = false;
}

void MuxQueue::shutdown() {
  Filter::shutdown();
  m_pool->shutdown();
}

void MuxQueue::process(Event *evt) {
  if (!m_has_error) {
    if (!m_request) {
      pjs::Value key;
      if (m_session_selector) {
        if (!Filter::eval(m_session_selector, key)) {
          m_has_error = true;
          return;
        }
      }
      m_queue = static_cast<Queue*>(m_pool->alloc(this, key));
      m_request = m_queue->alloc(Filter::output());
    }

    if (m_request) {
      m_request->input()->input(evt);
    }
  }
}

//
// MuxQueue::Request
//

void MuxQueue::Request::discard() {
  EventFunction::chain(nullptr);
}

void MuxQueue::Request::on_event(Event *evt) {
  if (auto s = Muxer::Stream::session()) {
    auto queue = static_cast<Queue*>(s);
    auto input = queue->m_pipeline->input();
    auto is_sending = m_is_sending;

    if (evt->is<MessageStart>()) {
      if (!m_started) {
        m_started = true;
        if (is_sending) {
          input->input(evt);
        } else {
          m_buffer.push(evt);
        }
      }

    } else if (evt->is<Data>()) {
      if (m_started && !m_ended) {
        if (is_sending) {
          input->input(evt);
        } else {
          m_buffer.push(evt);
        }
      }

    } else if (evt->is_end()) {
      if (m_started && !m_ended) {
        m_ended = true;
        if (evt->is<StreamEnd>()) {
          evt = MessageEnd::make();
        }
        if (is_sending) {
          input->input(evt);
          auto s = Muxer::Stream::next();
          while (s) {
            auto request = static_cast<Request*>(s);
            request->m_is_sending = true;
            request->m_buffer.flush([&](Event *evt) { input->input(evt); });
            if (!request->m_ended) break;
            s = s->next();
          }
        } else {
          m_buffer.push(evt);
        }
      }
    }
  }
}

//
// MuxQueue::Queue
//

MuxQueue::Queue::Queue(MuxQueue *mux) {
  allow_queuing(!mux->m_options.blocking);
  m_pipeline = mux->sub_pipeline(0, true, EventTarget::input());
  m_pipeline->on_eos([this](StreamEnd *) { Muxer::Session::abort(); });
  m_pipeline->start();
}

auto MuxQueue::Queue::alloc(EventTarget::Input *output) -> Request* {
  auto r = new Request();
  r->chain(output);
  Muxer::Session::append(r);
  if (auto back = r->back()) {
    auto last = static_cast<Request*>(back);
    if (last->m_is_sending && last->m_ended) {
      r->m_is_sending = true;
    }
  } else {
    r->m_is_sending = true;
  }
  return r->retain();
}

void MuxQueue::Queue::free(Request *r) {
  r->discard();
  Muxer::Session::remove(r);
  r->release();
}

void MuxQueue::Queue::free_all() {
  for (auto r = Muxer::Session::head(); r; ) {
    auto request = static_cast<Request*>(r); r = r->next();
    free(request);
  }
}

void MuxQueue::Queue::on_event(Event *evt) {
  if (auto r = Muxer::Session::head()) {
    auto request = static_cast<Request*>(r);
    auto output = request->output();

    if (evt->is<MessageStart>()) {
      if (!m_started) {
        m_started = true;
        output->input(evt);
      }

    } else if (evt->is<Data>()) {
      if (m_started) {
        output->input(evt);
      }

    } else if (evt->is<MessageEnd>()) {
      if (m_started) {
        m_started = false;
        output->input(evt);
        free(request);
      }

    } else if (evt->is<StreamEnd>()) {
      while (r) {
        static_cast<Request*>(r)->output()->input(evt);
        r = r->next();
      }
      free_all();
    }
  }
}

//
// MuxQueue::Pool
//

MuxQueue::Pool::Pool()
{
}

MuxQueue::Pool::Pool(const Options &options)
  : Muxer(options)
  , m_options(options)
{
}

auto MuxQueue::Pool::on_muxer_session_open(Filter *filter) -> Session* {
  auto s = new Queue(static_cast<MuxQueue*>(filter));
  return s->retain();
}

void MuxQueue::Pool::on_muxer_session_close(Session *session) {
  auto s = static_cast<Queue*>(session);
  s->free_all();
  s->release();
}

} // namespace pipy
