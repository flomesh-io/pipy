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
#include "pipeline.hpp"

namespace pipy {

//
// MuxBase
//

MuxBase::MuxBase()
  : m_session_manager(new SessionManager(this))
{
}

MuxBase::MuxBase(const pjs::Value &session_key, pjs::Object *options)
  : m_session_manager(new SessionManager(this))
  , m_session_key(session_key)
{
  if (options) {
    SessionManager::Options opts;

    pjs::Value max_idle;
    options->get("maxIdle", max_idle);

    if (!max_idle.is_undefined()) {
      if (!max_idle.is_number()) throw std::runtime_error("option.maxIdle expects a number");
      opts.max_idle = max_idle.n();
    }

    m_session_manager->set_options(opts);
  }
}

MuxBase::MuxBase(const MuxBase &r)
  : Filter(r)
  , m_session_manager(r.m_session_manager)
  , m_session_key(r.m_session_key)
{
}

void MuxBase::reset() {
  Filter::reset();
  if (m_session) {
    if (m_stream) {
      m_stream->chain(nullptr);
      m_session->close_stream(m_stream);
      m_stream = nullptr;
    }
    m_session_manager->free(m_session);
    m_session = nullptr;
  }
}

void MuxBase::process(Event *evt) {
  if (!m_stream) {
    auto session = m_session.get();
    if (!session) {
      pjs::Value key;
      if (!eval(m_session_key, key)) return;
      session = m_session_manager->get(key);
      m_session = session;
    }

    if (!session->m_pipeline) {
      auto p = sub_pipeline(0, true);
      p->chain(session->reply());
      session->m_pipeline = p;
      session->chain_forward(p->input());
      session->open();
    }

    auto s = session->open_stream();
    s->chain(output());
    m_stream = s;
  }

  output(evt, m_stream->input());
}

//
// MuxBase::Session
//

void MuxBase::Session::open()
{
}

void MuxBase::Session::close()
{
}

void MuxBase::Session::isolate() {
  if (m_manager) {
    m_manager->erase(this);
    m_manager = nullptr;
  }
}

void MuxBase::Session::on_input(Event *evt) {
  forward(evt);
}

void MuxBase::Session::on_reply(Event *evt) {
  if (evt->is<StreamEnd>()) {
    if (auto p = m_pipeline.get()) {
      Pipeline::auto_release(p);
      m_pipeline = nullptr;
    }
  }
  output(evt);
}

//
// MuxBase::SessionManager
//

auto MuxBase::SessionManager::get(const pjs::Value &key) -> Session* {
  Session *session = nullptr;
  if (key.is_object() && key.o()) {
    pjs::WeakRef<pjs::Object> o(key.o());
    auto i = m_weak_sessions.find(o);
    if (i != m_weak_sessions.end()) {
      session = i->second;
      if (!i->first.ptr()) {
        close(session);
        session = nullptr;
      }
    }
  } else {
    auto i = m_sessions.find(key);
    if (i != m_sessions.end()) {
      session = i->second;
    }
  }

  if (session) {
    if (!session->m_share_count) {
      m_free_sessions.remove(session);
    }
    session->m_share_count++;
    return session;
  }

  session = m_mux->on_new_session();
  session->m_manager = this;

  if (key.is_object() && key.o()) {
    session->m_weak_key = key.o();
    m_weak_sessions[key.o()] = session;
  } else {
    session->m_key = key;
    m_sessions[key] = session;
  }

  return session;
}

void MuxBase::SessionManager::free(Session *session) {
  if (session->m_manager == this) {
    if (!--session->m_share_count) {
      session->m_free_time = utils::now();
      m_free_sessions.push(session);
      recycle();
    }
  }
}

void MuxBase::SessionManager::erase(Session *session) {
  if (session->m_weak_key.init_ptr()) {
    m_weak_sessions.erase(session->m_weak_key);
  } else {
    m_sessions.erase(session->m_key);
  }
}

void MuxBase::SessionManager::close(Session *session) {
  session->m_pipeline = nullptr;
  session->close();
  erase(session);
}

void MuxBase::SessionManager::recycle() {
  if (m_recycling) return;
  if (m_free_sessions.empty()) return;

  m_recycle_timer.schedule(
    1.0,
    [this]() {
      m_recycling = false;
      auto now = utils::now();
      auto s = m_free_sessions.head();
      while (s) {
        auto session = s; s = s->next();
        if (now - session->m_free_time >= m_options.max_idle * 1000) {
          m_free_sessions.remove(session);
          close(session);
        }
      }
      recycle();
      release();
    }
  );

  retain();
  m_recycling = true;
}

//
// QueueMuxer
//

auto QueueMuxer::open() -> EventFunction* {
  auto s = new Stream(this);
  s->retain();
  return s;
}

void QueueMuxer::close(EventFunction *stream) {
  auto s = static_cast<Stream*>(stream);
  s->release();
}

void QueueMuxer::reset() {
  while (auto s = m_streams.head()) {
    m_streams.remove(s);
    s->release();
  }
  m_isolated = false;
}

void QueueMuxer::isolate() {
  m_isolated = true;
}

void QueueMuxer::on_event(Event *evt) {
  if (m_isolated) {
    if (auto s = m_streams.head()) {
      s->output(evt);
    }
    return;
  }

  if (evt->is<MessageStart>()) {
    if (auto s = m_streams.head()) {
      if (!s->m_started) {
        s->output(evt);
        s->m_started = true;
      }
    }

  } else if (evt->is<Data>()) {
    if (auto s = m_streams.head()) {
      if (s->m_started) {
        s->output(evt);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (auto s = m_streams.head()) {
      if (s->m_started) {
        m_streams.remove(s);
        s->output(evt);
        s->release();
      }
    }

  } else if (auto end = evt->as<StreamEnd>()) {
    while (auto s = m_streams.head()) {
      m_streams.remove(s);
      if (!s->m_started) {
        s->output(MessageStart::make());
      }
      s->output(evt->clone());
      s->release();
    }
  }
}

//
// QueueMuxer::Stream
//

void QueueMuxer::Stream::on_event(Event *evt) {
  auto muxer = m_muxer;

  if (m_isolated) {
    muxer->output(evt);
    return;
  }

  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start && !m_queued) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (!m_queued) {
      retain();
      m_queued = true;
      muxer->m_streams.push(this);
      muxer->output(m_start);
      if (!m_buffer.empty()) {
        muxer->output(Data::make(m_buffer));
        m_buffer.clear();
      }
      muxer->output(MessageEnd::make());
      if (muxer->m_isolated) m_isolated = true;
    }
  }
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(const pjs::Value &key, pjs::Object *options)
  : MuxBase(key, options)
{
}

Mux::Mux(const Mux &r)
  : MuxBase(r)
{
}

Mux::~Mux() {
}

void Mux::dump(std::ostream &out) {
  out << "mux";
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::on_new_session() -> MuxBase::Session* {
  return new Session();
}

//
// Mux::Session
//

void Mux::Session::open() {
  QueueMuxer::chain(MuxBase::Session::input());
  MuxBase::Session::chain(QueueMuxer::reply());
}

auto Mux::Session::open_stream() -> EventFunction* {
  return QueueMuxer::open();
}

void Mux::Session::close_stream(EventFunction *stream) {
  return QueueMuxer::close(stream);
}

void Mux::Session::close() {
  QueueMuxer::reset();
}

} // namespace pipy
