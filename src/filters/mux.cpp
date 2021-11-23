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

MuxBase::MuxBase(const pjs::Value &key, pjs::Object *options)
  : m_session_manager(new SessionManager(this))
  , m_target_key(key)
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
  , m_target_key(r.m_target_key)
{
}

void MuxBase::reset() {
  Filter::reset();
  if (m_stream) {
    m_stream->chain(nullptr);
    m_stream->close();
    m_stream = nullptr;
  }
  if (m_session) {
    m_session_manager->free(m_session);
    m_session = nullptr;
  }
}

void MuxBase::process(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_stream) {
      if (!m_session) {
        pjs::Value key;
        if (!eval(m_target_key, key)) return;
        m_session = m_session_manager->get(key);
      }

      if (!m_session->m_pipeline) {
        auto p = sub_pipeline(0, true);
        m_session_manager->open(m_session, p);
      }

      auto s = m_session->stream();
      s->chain(output());
      output(start, s->input());
      m_stream = s;
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_stream) {
      output(evt, m_stream->input());
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_stream) {
      output(evt, m_stream->input());
      m_stream->close();
      m_stream = nullptr;
    }

  } else if (evt->is<StreamEnd>()) {
    if (m_stream) {
      output(MessageEnd::make(), m_stream->input());
      m_stream->close();
      m_stream = nullptr;
    }
  }
}

//
// MuxBase::Demux
//

void MuxBase::Demux::on_event(Event *evt) {
  static_cast<MuxBase::Session*>(this)->on_demux(evt);
}

//
// MuxBase::Session
//

void MuxBase::Session::input(Event *evt) {
  if (m_pipeline) {
    if (auto inp = m_pipeline->input()) {
      inp->input(evt);
    }
  }
}

void MuxBase::Session::open(Pipeline *pipeline) {
  pipeline->chain(Demux::input());
}

void MuxBase::Session::close() {
  Pipeline::auto_release(m_pipeline);
  m_pipeline = nullptr;
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
        m_weak_sessions.erase(i);
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
      retain_for_free_sessions();
    }
    session->m_share_count++;
    return session;
  }
  session = m_mux->on_new_session();
  if (key.is_object() && key.o()) {
    m_weak_sessions[key.o()] = session;
  } else {
    m_sessions[key] = session;
  }
  return session;
}

void MuxBase::SessionManager::free(Session *session) {
  if (!--session->m_share_count) {
    session->m_free_time = utils::now();
    m_free_sessions.push(session);
    retain_for_free_sessions();
  }
}

void MuxBase::SessionManager::open(Session *session, Pipeline *pipeline) {
  session->m_pipeline = pipeline;
  session->open(pipeline);
}

void MuxBase::SessionManager::close(Session *session) {
  session->close();
}

void MuxBase::SessionManager::retain_for_free_sessions() {
  if (m_retained_for_free_sessions) {
    if (m_free_sessions.empty()) {
      release();
      m_retained_for_free_sessions = false;
    }
  } else {
    if (!m_free_sessions.empty()) {
      retain();
      m_retained_for_free_sessions = true;
    }
  }
}

void MuxBase::SessionManager::recycle() {
  auto now = utils::now();
  auto s = m_free_sessions.head();
  while (s) {
    auto session = s; s = s->next();
    if (now - session->m_free_time >= m_options.max_idle * 1000) {
      m_free_sessions.remove(session);
      close(session);
    }
  }

  m_recycle_timer.schedule(1, [this]() { recycle(); });

  retain_for_free_sessions();
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

//
// Mux::Session
//

auto Mux::Session::stream() -> Stream* {
  return new Stream(this);
}

void Mux::Session::on_demux(Event *evt) {

  if (evt->is<MessageStart>()) {
    if (auto stream = m_streams.head()) {
      if (!stream->m_started) {
        stream->output()->input(evt);
        stream->m_started = true;
      }
    }

  } else if (evt->is<Data>()) {
    if (auto stream = m_streams.head()) {
      if (stream->m_started) {
        stream->output()->input(evt);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (auto stream = m_streams.head()) {
      if (stream->m_started) {
        m_streams.remove(stream);
        stream->output()->input(evt);
        delete stream;
      }
    }

  } else if (auto end = evt->as<StreamEnd>()) {
    close(end);
  }
}

void Mux::Session::close() {
  close(StreamEnd::make());
}

void Mux::Session::close(StreamEnd *end) {
  pjs::Ref<StreamEnd> ref(end);
  MuxBase::Session::close();
  List<Stream> streams(std::move(m_streams));
  while (auto s = streams.head()) {
    streams.remove(s);
    if (!s->m_started) s->output()->input(MessageStart::make());
    s->output()->input(end);
    delete s;
  }
}

//
// Mux::Session::Stream
//

void Mux::Session::Stream::on_event(Event *evt) {
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
      m_session->m_streams.push(this);
      m_queued = true;
      m_session->input(m_start);
      if (!m_buffer.empty()) {
        m_session->input(Data::make(m_buffer));
        m_buffer.clear();
      }
      m_session->input(MessageEnd::make());
    }
  }
}

void Mux::Session::Stream::close() {
  if (!m_queued) {
    delete this;
  }
}

} // namespace pipy
