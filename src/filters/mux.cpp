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
#include "input.hpp"
#include "utils.hpp"

namespace pipy {

//
// MuxBase::Options
//

MuxBase::Options::Options(pjs::Object *options) {
  Value(options, "maxIdle")
    .get_seconds(max_idle)
    .check_nullable();
}

//
// MuxBase
//

MuxBase::MuxBase()
  : m_session_manager(new SessionManager(this))
{
}

MuxBase::MuxBase(pjs::Function *group, const Options &options)
  : m_session_manager(new SessionManager(this))
  , m_group(group)
{
  m_session_manager->set_max_idle(options.max_idle);
}

MuxBase::MuxBase(const MuxBase &r)
  : Filter(r)
  , m_session_manager(r.m_session_manager)
  , m_group(r.m_group)
{
}

void MuxBase::reset() {
  Filter::reset();
  if (m_session) {
    stop_waiting();
    if (m_stream) {
      m_stream->chain(nullptr);
      m_session->close_stream(m_stream);
      m_stream = nullptr;
    }
    m_session->free();
    m_session = nullptr;
  }
  m_waiting_events.clear();
  m_session_key = pjs::Value::undefined;
}

void MuxBase::process(Event *evt) {
  if (!m_stream) {
    auto session = m_session.get();
    if (!session) {
      if (m_group && !eval(m_group, m_session_key)) return;
      if (m_session_key.is_undefined()) {
        m_session_key.set(context()->inbound());
      }
      session = m_session_manager->get(m_session_key);
      m_session = session;
    }

    if (!session->m_pipeline) {
      auto p = sub_pipeline(0, true);
      session->init(p);
    }

    if (session->is_pending()) {
      start_waiting();
      m_waiting_events.push(evt);
      return;
    }

    open_stream();
  }

  output(evt, m_stream->input());
}

void MuxBase::open_stream() {
  auto s = m_session->open_stream();
  s->chain(output());
  m_stream = s;
}

void MuxBase::start_waiting() {
  if (!m_waiting) {
    m_session->m_waiting_muxers.push(this);
    m_waiting = true;
  }
}

void MuxBase::flush_waiting() {
  open_stream();
  m_waiting_events.flush(
    [this](Event *evt) {
      output(evt, m_stream->input());
    }
  );
  stop_waiting();
}

void MuxBase::stop_waiting() {
  if (m_waiting) {
    m_session->m_waiting_muxers.remove(this);
    m_waiting = false;
  }
}

//
// MuxBase::Session
//
// Construction:
//   - When a new session key is requested
//
// Destruction:
//   - When share count is 0 for a time of maxIdle
//   - When freed by MuxBase after being isolated
//
// Session owns streams:
//   - Session::open_stream(): Creates a new stream
//   - Session::close_stream(): Destroys an existing stream
//

void MuxBase::Session::open()
{
}

void MuxBase::Session::close()
{
}

void MuxBase::Session::isolate() {
  if (auto manager = m_manager) {
    m_manager = nullptr;
    manager->erase(this);
  }
}

void MuxBase::Session::set_pending(bool pending) {
  if (pending != m_is_pending) {
    if (!pending) {
      for (auto *p = m_waiting_muxers.head(); p; ) {
        auto *muxer = p; p = p->List<MuxBase>::Item::next();
        muxer->flush_waiting();
      }
    }
    m_is_pending = pending;
  }
}

void MuxBase::Session::init(Pipeline *pipeline) {
  m_pipeline = pipeline;
  pipeline->chain(reply());
  chain_forward(pipeline->input());
  open();
}

void MuxBase::Session::free() {
  if (m_manager) {
    m_manager->free(this);
  } else {
    reset();
  }
}

void MuxBase::Session::reset() {
  if (auto p = m_pipeline.get()) {
    close();
    Pipeline::auto_release(p);
    m_pipeline = nullptr;
  }
  isolate();
}

void MuxBase::Session::on_input(Event *evt) {
  forward(evt);
}

void MuxBase::Session::on_reply(Event *evt) {
  output(evt);
  if (evt->is<StreamEnd>()) {
    reset();
  }
}

//
// MuxBase::SessionManager
//

auto MuxBase::SessionManager::get(const pjs::Value &key) -> Session* {
  bool is_weak = (key.is_object() && key.o());
  Session *session = nullptr;

  if (is_weak) {
    pjs::WeakRef<pjs::Object> o(key.o());
    auto i = m_weak_sessions.find(o);
    if (i != m_weak_sessions.end()) {
      session = i->second;
      if (!i->first.ptr()) {
        session->reset();
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
    if (session->is_free()) {
      m_free_sessions.remove(session);
    }
    session->m_share_count++;
    return session;
  }

  session = m_mux->on_new_session();
  session->m_manager = this;

  if (is_weak) {
    session->m_weak_key = key.o();
    session->watch(key.o()->weak_ptr());
    m_weak_sessions[key.o()] = session;
  } else {
    session->m_key = key;
    m_sessions[key] = session;
  }

  return session;
}

void MuxBase::SessionManager::free(Session *session) {
  if (!--session->m_share_count) {
    session->m_free_time = utils::now();
    m_free_sessions.push(session);
    recycle();
  }
}

void MuxBase::SessionManager::erase(Session *session) {
  Session::auto_release(session);
  if (session->is_free()) {
    m_free_sessions.remove(session);
  }
  if (session->m_weak_key.original_ptr()) {
    m_weak_sessions.erase(session->m_weak_key);
  } else {
    m_sessions.erase(session->m_key);
  }
}

void MuxBase::SessionManager::recycle() {
  if (m_recycling) return;
  if (m_free_sessions.empty()) return;

  m_recycle_timer.schedule(
    1.0,
    [this]() {
      InputContext ic;
      m_recycling = false;
      auto now = utils::now();
      auto s = m_free_sessions.head();
      while (s) {
        auto session = s; s = s->next();
        if (!session->m_weak_key) {
          if (now - session->m_free_time >= m_max_idle * 1000) {
            session->input()->input(StreamEnd::make());
            session->reset();
          }
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

void QueueMuxer::on_reply(Event *evt) {
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
// Retain:
//   - Session::open_stream()
//   - After queued
// Release:
//   - Session::close_stream()
//   - After replied
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
    if (m_start && !m_queued) {
      auto *end = evt->as<MessageEnd>();
      retain();
      m_queued = true;
      muxer->m_streams.push(this);
      muxer->output(m_start);
      if (!m_buffer.empty()) {
        muxer->output(Data::make(std::move(m_buffer)));
      }
      muxer->output(end ? end : MessageEnd::make());
      if (muxer->m_isolated) m_isolated = true;
    }
  }
}

//
// MuxQueue
//

MuxQueue::MuxQueue()
{
}

MuxQueue::MuxQueue(pjs::Function *group, const Options &options)
  : MuxBase(group, options)
{
}

MuxQueue::MuxQueue(const MuxQueue &r)
  : MuxBase(r)
{
}

MuxQueue::~MuxQueue() {
}

void MuxQueue::dump(Dump &d) {
  Filter::dump(d);
  d.name = "muxQueue";
  d.sub_type = Dump::MUX;
}

auto MuxQueue::clone() -> Filter* {
  return new MuxQueue(*this);
}

auto MuxQueue::on_new_session() -> MuxBase::Session* {
  return new Session();
}

//
// MuxQueue::Session
//

void MuxQueue::Session::open() {
  QueueMuxer::chain(MuxBase::Session::input());
  MuxBase::Session::chain(QueueMuxer::reply());
}

auto MuxQueue::Session::open_stream() -> EventFunction* {
  return QueueMuxer::open();
}

void MuxQueue::Session::close_stream(EventFunction *stream) {
  return QueueMuxer::close(stream);
}

void MuxQueue::Session::close() {
  QueueMuxer::reset();
}

//
// Mux
//

Mux::Mux(pjs::Function *group, pjs::Object *options)
  : MuxBase(group, options)
{
}

Mux::Mux(const Mux &r)
  : MuxBase(r)
{
}

Mux::~Mux()
{
}

void Mux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "mux";
  d.sub_type = Dump::MUX;
  d.out_type = Dump::OUTPUT_FROM_SELF;
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

void Mux::process(Event *evt) {
  MuxBase::process(evt->clone());
  output(evt);
}

auto Mux::on_new_session() -> MuxBase::Session* {
  return new Session();
}

//
// Mux::Session
//

auto Mux::Session::open_stream() -> EventFunction* {
  return new Stream(this);
}

void Mux::Session::close_stream(EventFunction *stream) {
  delete static_cast<Stream*>(stream);
}

//
// Mux::Stream
//

void Mux::Stream::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_start) {
      auto inp = m_output.get();
      inp->input(m_start);
      if (!m_buffer.empty()) {
        inp->input(Data::make(m_buffer));
        m_buffer.clear();
      }
      inp->input(MessageEnd::make());
    }
  }
}

} // namespace pipy
