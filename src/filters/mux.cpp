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
#include "log.hpp"

#include <limits>

namespace pipy {

//
// MuxBase::Options
//

MuxBase::Options::Options(pjs::Object *options) {
  static pjs::ConstStr s_max_idle("maxIdle");
  static pjs::ConstStr s_max_queue("maxQueue");
  Value(options, s_max_idle)
    .get_seconds(max_idle)
    .check_nullable();
  Value(options, s_max_queue)
    .get(max_queue)
    .check_nullable();
}

//
// MuxBase
//

MuxBase::MuxBase()
  : m_session_manager(new SessionManager())
{
}

MuxBase::MuxBase(pjs::Function *group)
  : m_session_manager(new SessionManager())
  , m_group(group)
{
}

MuxBase::MuxBase(pjs::Function *group, const Options &options)
  : m_options(options)
  , m_session_manager(new SessionManager())
  , m_group(group)
{
}

MuxBase::MuxBase(pjs::Function *group, pjs::Function *options)
  : m_options_f(options)
  , m_session_manager(new SessionManager())
  , m_group(group)
{
}

MuxBase::MuxBase(const MuxBase &r)
  : Filter(r)
  , m_options(r.m_options)
  , m_options_f(r.m_options_f)
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

void MuxBase::shutdown() {
  m_session_manager->shutdown();
}

void MuxBase::process(Event *evt) {
  if (!m_stream) {
    auto session = m_session.get();
    if (!session) {
      if (m_group && !eval(m_group, m_session_key)) return;
      if (m_session_key.is_undefined()) {
        m_session_key.set(context()->inbound());
      }
      session = m_session_manager->get(this, m_session_key);
      if (!session) return;
      m_session = session;
    }

    if (!session->m_pipeline) {
      pjs::Value args[2];
      args[0] = m_session_key;
      args[1].set((int)session->m_cluster->m_sessions.size());
      auto p = sub_pipeline(0, true, session->reply(), nullptr, 2, args);
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

void MuxBase::Session::close() {
  forward(StreamEnd::make());
}

void MuxBase::Session::isolate() {
  if (auto cluster = m_cluster) {
    m_cluster = nullptr;
    cluster->discard(this);
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
  chain_forward(pipeline->input());
  open();
}

void MuxBase::Session::free() {
  if (m_cluster) {
    m_cluster->free(this);
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
    m_is_closed = true;
  }
}

//
// MuxBase::SessionCluster
//

MuxBase::SessionCluster::SessionCluster(MuxBase *mux, pjs::Object *options) {
  if (options) {
    Options opts(options);
    m_max_idle = opts.max_idle;
    m_max_queue = opts.max_queue;
  } else {
    m_max_idle = mux->m_options.max_idle;
    m_max_queue = mux->m_options.max_queue;
  }
}

auto MuxBase::SessionCluster::alloc() -> Session* {
  auto max_share_count = m_max_queue;
  auto *s = m_sessions.head();
  while (s) {
    if (max_share_count <= 0 || s->m_share_count < max_share_count) {
      s->m_share_count++;
      sort(s);
      return s;
    }
    s = s->next();
  }
  s = session();
  s->m_cluster = this;
  s->retain();
  m_sessions.unshift(s);
  return s;
}

void MuxBase::SessionCluster::free(Session *session) {
  session->m_share_count--;
  if (session->is_free()) {
    session->m_free_time = utils::now();
  }
  sort(session);
}

void MuxBase::SessionCluster::discard(Session *session) {
  Session::auto_release(session);
  m_sessions.remove(session);
  session->release();
  sort(nullptr);
}

void MuxBase::SessionCluster::sort(Session *session) {
  if (session) {
    auto p = session->back();
    while (p && p->m_share_count > session->m_share_count) p = p->back();
    if (p == session->back()) {
      auto p = session->next();
      while (p && p->m_share_count < session->m_share_count) p = p->next();
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

  schedule_recycling();

  if (m_sessions.empty()) {
    if (m_weak_key.original_ptr()) {
      m_manager->m_weak_clusters.erase(m_weak_key);
    } else {
      m_manager->m_clusters.erase(m_key);
    }
    free();
  }
}

void MuxBase::SessionCluster::schedule_recycling() {
  auto s = m_sessions.head();
  if (!s || s->m_share_count > 0) {
    if (m_recycle_scheduled) {
      m_manager->m_recycle_clusters.remove(this);
      m_recycle_scheduled = false;
    }
  } else {
    if (!m_recycle_scheduled) {
      m_manager->m_recycle_clusters.push(this);
      m_manager->recycle();
      m_recycle_scheduled = true;
    }
  }
}

void MuxBase::SessionCluster::recycle(double now) {
  auto max_idle = m_max_idle * 1000;
  auto s = m_sessions.head();
  while (s) {
    auto session = s; s = s->next();
    if (session->m_share_count > 0) break;
    if (session->m_is_closed || m_weak_ptr_gone || now - session->m_free_time >= max_idle) {
      session->reset();
    }
  }
}

void MuxBase::SessionCluster::on_weak_ptr_gone() {
  m_weak_ptr_gone = true;
  m_manager->m_weak_clusters.erase(m_weak_key);
  schedule_recycling();
}

//
// MuxBase::SessionManager
//

MuxBase::SessionManager::~SessionManager() {
  for (const auto &p : m_clusters) p.second->free();
  for (const auto &p : m_weak_clusters) p.second->free();
}

auto MuxBase::SessionManager::get(MuxBase *mux, const pjs::Value &key) -> Session* {
  bool is_weak = (key.is_object() && key.o());
  SessionCluster *cluster = nullptr;

  if (is_weak) {
    pjs::WeakRef<pjs::Object> o(key.o());
    auto i = m_weak_clusters.find(o);
    if (i != m_weak_clusters.end()) {
      cluster = i->second;
    }
  } else {
    auto i = m_clusters.find(key);
    if (i != m_clusters.end()) {
      cluster = i->second;
    }
  }

  if (cluster) return cluster->alloc();

  try {
    pjs::Value opts;
    pjs::Object *options = nullptr;
    if (auto f = mux->m_options_f.get()) {
      if (!mux->eval(f, opts)) return nullptr;
      if (!opts.is_object()) {
        Log::error("[mux] options callback did not return an object");
        return nullptr;
      }
      options = opts.o();
    }

    cluster = mux->on_new_cluster(options);
    cluster->m_manager = this;

  } catch (std::runtime_error &err) {
    Log::error("[mux] %s", err.what());
    return nullptr;
  }

  if (is_weak) {
    cluster->m_weak_key = key.o();
    cluster->watch(key.o()->weak_ptr());
    m_weak_clusters[key.o()] = cluster;
  } else {
    cluster->m_key = key;
    m_clusters[key] = cluster;
  }

  return cluster->alloc();
}

void MuxBase::SessionManager::shutdown() {
  m_has_shutdown = true;
}

void MuxBase::SessionManager::recycle() {
  if (m_recycling) return;
  if (m_recycle_clusters.empty()) return;

  m_recycle_timer.schedule(
    1.0,
    [this]() {
      InputContext ic;
      m_recycling = false;
      auto now = m_has_shutdown ? std::numeric_limits<double>::infinity() : utils::now();
      auto c = m_recycle_clusters.head();
      while (c) {
        auto cluster = c; c = c->next();
        cluster->recycle(now);
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

void QueueMuxer::increase_queue_count() {
  if (auto s = m_streams.head()) {
    s->m_queued_count++;
  }
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
      s->m_isolated = true;
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
        if (!--s->m_queued_count) {
          m_streams.remove(s);
          s->output(evt);
          s->release();
        } else {
          s->m_started = false;
          s->output(evt);
        }
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
    if (m_start && !m_queued_count) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_start && !m_queued_count) {
      auto *end = evt->as<MessageEnd>();
      retain();
      m_queued_count = 1;
      muxer->m_streams.push(this);
      muxer->output(m_start);
      if (!m_buffer.empty()) {
        muxer->output(Data::make(std::move(m_buffer)));
      }
      muxer->output(end ? end : MessageEnd::make());
    }
  }
}

//
// MuxQueue
//

MuxQueue::MuxQueue()
{
}

MuxQueue::MuxQueue(pjs::Function *group)
  : MuxBase(group)
{
}

MuxQueue::MuxQueue(pjs::Function *group, const Options &options)
  : MuxBase(group, options)
{
}

MuxQueue::MuxQueue(pjs::Function *group, pjs::Function *options)
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

auto MuxQueue::on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* {
  return new SessionCluster(this, options);
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
  MuxBase::Session::close();
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(pjs::Function *group)
  : MuxBase(group)
{
}

Mux::Mux(pjs::Function *group, const Options &options)
  : MuxBase(group, options)
{
}

Mux::Mux(pjs::Function *group, pjs::Function *options)
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

auto Mux::on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* {
  return new SessionCluster(this, options);
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
