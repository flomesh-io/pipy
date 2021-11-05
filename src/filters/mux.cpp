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
        auto pipeline = sub_pipeline(0, true);
        m_session->m_pipeline = pipeline;
        m_session->open(pipeline);
      }

      m_stream = m_session->stream(start);
      m_stream->chain(output());
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_stream) {
      output(evt, m_stream->input());
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_stream) {
      output(MessageEnd::make(), m_stream->input());
      m_stream->close();
      m_stream = nullptr;
    }
  }
}

//
// MuxBase::Session
//

void MuxBase::Session::on_event(Event *evt) {
  if (m_pipeline) {
    auto inp = m_pipeline->input();
    output(evt, inp);
  }
}

void MuxBase::Session::close() {
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
  auto session = m_mux->new_session();
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

void Mux::Session::open(Pipeline *pipeline) {
  pipeline->chain(m_ef_demux.input());
}

auto Mux::Session::stream(MessageStart *start) -> Stream* {
  return new Stream(this, start);
}

void Mux::Session::on_demux(Event *evt) {

  if (evt->is<MessageStart>()) {
    while (auto stream = m_streams.head()) {
      if (!stream->m_output_end) break;
      m_streams.remove(stream);
      delete stream;
    }
    if (auto stream = m_streams.head()) {
      if (!m_message_started) {
        output(evt, stream->output());
        m_message_started = true;
      }
    }

  } else if (evt->is<Data>()) {
    if (auto stream = m_streams.head()) {
      if (m_message_started) {
        output(evt, stream->output());
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (auto stream = m_streams.head()) {
      if (m_message_started) {
        output(evt, stream->output());
        stream->m_output_end = true;
        m_message_started = false;
      }
    }

  } else if (evt->is<StreamEnd>()) {
    MuxBase::Session::close();
    m_message_started = false;
    List<Stream> streams(std::move(m_streams));
    while (auto s = streams.head()) {
      streams.remove(s);
      if (!s->m_output_end) {
        output(evt, s->output());
      }
      delete s;
    }
  }
}

void Mux::Session::close() {
  MuxBase::Session::close();
  m_message_started = false;
  while (auto stream = m_streams.head()) {
    m_streams.remove(stream);
    delete stream;
  }
}

//
// Mux::Session::Stream
//

void Mux::Session::Stream::on_event(Event *evt) {
  if (auto data = evt->as<Data>()) {
    if (!m_queued) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (!m_queued) {
      auto out = m_session->input();
      m_session->m_streams.push(this);
      m_queued = true;
      output(m_start, out);
      if (!m_buffer.empty()) output(Data::make(m_buffer), out);
      output(evt, out);
      m_start = nullptr;
      m_buffer.clear();
    }
  }
}

void Mux::Session::Stream::close() {
  if (!m_queued) {
    delete this;
  }
}

} // namespace pipy
