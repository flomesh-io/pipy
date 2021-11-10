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
  : m_session_manager(std::make_shared<SessionManager>(this))
{
}

MuxBase::MuxBase(const pjs::Value &key)
  : m_session_manager(std::make_shared<SessionManager>(this))
  , m_target_key(key)
{
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
        m_session->m_pipeline = p;
        m_session->open(p);
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
  auto i = m_sessions.find(key);
  if (i != m_sessions.end()) {
    auto session = i->second;
    if (!session->m_share_count) {
      m_free_sessions.erase(session);
    }
    session->m_share_count++;
    return session;
  }
  auto session = m_mux->on_new_session();
  m_sessions[key] = session;
  return session;
}

void MuxBase::SessionManager::free(Session *session) {
  if (!--session->m_share_count) {
    session->m_free_time = utils::now();
    m_free_sessions.insert(session);
  }
}

void MuxBase::SessionManager::recycle() {
  auto now = utils::now();
  auto i = m_free_sessions.begin();
  while (i != m_free_sessions.end()) {
    auto j = i++;
    auto session = *j;
    if (now - session->m_free_time >= 10*1000) {
      m_free_sessions.erase(j);
      session->close();
      session->m_pipeline = nullptr;
    }
  }

  m_recycle_timer.schedule(1, [this]() { recycle(); });
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(const pjs::Value &key)
  : MuxBase(key)
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
        stream->output()->input(evt);
        m_streams.remove(stream);
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
