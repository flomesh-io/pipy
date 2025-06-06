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
#include "net.hpp"
#include "utils.hpp"

#include "api/console.hpp"

#include <limits>

//
// All mux filters should derive from MuxSource.
// The main mux logic should be implemented in MuxSession, tho.
//
// +----------+      +-----------+      +---------------+      +----------------+      +----------+
// | Pipeline |----->| MuxSource |----->| EventFunction |----->|   MuxSession   |----->| Pipeline |
// | (client) |<-----| (filter)  |<-----|   (stream)    |<-----| (client/agent) |<-----| (server) |
// +----------+      +-----------+      +---------------+      +----------------+      +----------+
//
// MuxSource manages a single stream (as an EventFunction), which is allocated from a MuxSession.
// The MuxSession is allocated from a MuxSessionPool.
// The MuxSessionPool is allocated with a given session key from the MuxSessionMap.
// The MuxSessionMap is shared among all MuxSources that are copy-constructed from the same MuxSource.
//
// +-------------+ 1
// |             |----------------------------------------------------------------+
// |  MuxSource  |                                                                |
// |             |----------------------------------------+                       | ptr
// +-------------+ *                                      |                       | mux_session_open_stream()
//      * |                                               | ref                   | mux_session_close_stream()
//        | ref                                           |                       |
//      1 V                                             1 V                     1 V
// +---------------+  ptr  +----------------+       +------------+       +------------------------+
// |               |------>|                | 1   * |            | 1   * |                        |
// | MuxSessionMap | 1   * | MuxSessionPool |------>| MuxSession |------>| EventFunction (stream) |
// |               |<------|                |  ref  |            |       |                        |
// +---------------+  ref  +----------------+       +------------+       +------------------------+
//
// A MuxSource needs to implement:
//
//   - on_mux_new_pool()
//   - on_mux_new_pipeline()
//
//   > Note about pending sessions:
//   >
//   > After creation, a MuxSession can be in "pending mode" due to any asynchronous
//   > operations going on during the setup of the session, such as a TLS handshake
//   > before the actual protocol being used is decided via ALPN. In such case, all
//   > input events are buffered up and won't be written to the session until pending
//   > mode is over.
//
// A MuxSession needs to implement:
//
//   - mux_session_open()
//   - mux_session_open_stream()
//   - mux_session_close_stream()
//   - mux_session_close()
//
// A MuxSessionPool needs to implement:
//
//   - session() : Creates a new MuxSession
//   - free() : Deletes the MuxSessionPool
//

namespace pipy {

//
// MuxSession::Options
//

MuxSession::Options::Options(pjs::Object *options) {
  thread_local static pjs::ConstStr s_max_idle("maxIdle");
  thread_local static pjs::ConstStr s_max_queue("maxQueue");
  thread_local static pjs::ConstStr s_max_messages("maxMessages");
  Value(options, s_max_idle)
    .get_seconds(max_idle)
    .check_nullable();
  Value(options, s_max_queue)
    .get(max_queue)
    .check_nullable();
  Value(options, s_max_messages)
    .get(max_messages)
    .check_nullable();
}

//
// MuxSession
//
// Referenced and retained by:
//   - MuxSources
//   - MuxSessionPools
//

void MuxSession::increase_share_count() {
  m_share_count++;
  m_pool->sort(this);
}

void MuxSession::decrease_share_count() {
  if (m_pool) {
    m_pool->free(this);
  } else {
    close();
  }
}

void MuxSession::set_pending(bool pending) {
  if (pending != m_is_pending) {
    if (!pending) {
      for (auto *p = m_waiting_sources.head(); p; ) {
        auto *s = p; p = p->List<MuxSource>::Item::next();
        s->flush_waiting();
      }
    }
    m_is_pending = pending;
  }
}

void MuxSession::detach() {
  if (auto p = m_pool) {
    m_pool = nullptr;
    p->detach(this); // potentially deleted
  }
}

void MuxSession::end(StreamEnd *eos) {
  m_eos = eos;
  close();
  detach();
}

void MuxSession::open(MuxSource *source, Pipeline *pipeline) {
  EventProxy::chain_forward(pipeline->input());
  pipeline->chain(EventSource::reply());
  m_pipeline = pipeline;
  mux_session_open(source);
  auto *wk = m_pool->m_weak_key.get();
  pjs::Value arg(wk ? wk->ptr() : m_pool->m_key);
  pipeline->start(1, &arg);
}

void MuxSession::close() {
  if (m_pipeline) {
    mux_session_close();
    m_pipeline = nullptr;
  }
}

//
// MuxSessionPool
//
// Will be deleted when the number of MuxSessions goes down to zero.
//

MuxSessionPool::MuxSessionPool(const MuxSession::Options &options) {
  m_max_idle = options.max_idle;
  m_max_queue = options.max_queue;
  m_max_messages = options.max_messages;
}

auto MuxSessionPool::alloc() -> MuxSession* {
  auto max_share_count = m_max_queue;
  auto max_message_count = m_max_messages;
  auto *s = m_sessions.head();
  while (s) {
    if ((max_share_count <= 0 || s->m_share_count < max_share_count) &&
        (max_message_count <= 0 || s->m_message_count < max_message_count)
      ) {
      s->m_message_count++;
      return s;
    }
    s = s->next();
  }
  s = session();
  s->retain();
  s->m_pool = this;
  m_sessions.unshift(s);
  return s;
}

void MuxSessionPool::free(MuxSession *session) {
  session->m_share_count--;
  if (session->is_free()) {
    session->m_free_time = utils::now();
  }
  sort(session);
}

void MuxSessionPool::detach(MuxSession *session) {
  m_sessions.remove(session);
  session->release();
  sort(nullptr);
}

void MuxSessionPool::sort(MuxSession *session) {
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
    if (m_weak_key) {
      m_map->m_weak_pools.erase(m_weak_key);
    } else {
      m_map->m_pools.erase(m_key);
    }
    free();
  }
}

void MuxSessionPool::schedule_recycling() {
  auto s = m_sessions.head();
  if (!s || s->m_share_count > 0) {
    if (m_recycle_scheduled) {
      m_map->m_recycle_pools.remove(this);
      m_recycle_scheduled = false;
    }
  } else {
    if (!m_recycle_scheduled) {
      m_map->m_recycle_pools.push(this);
      m_map->schedule_recycling();
      m_recycle_scheduled = true;
    }
  }
}

void MuxSessionPool::recycle(double now) {
  auto max_idle = m_max_idle * 1000;
  auto s = m_sessions.head();
  while (s) {
    auto session = s; s = s->next();
    if (session->m_share_count > 0) break;
    if (session->m_is_pending || m_weak_ptr_gone ||
       (m_max_messages > 0 && session->m_message_count >= m_max_messages) ||
       (now - session->m_free_time >= max_idle))
    {
      MuxSession::auto_release(session);
      session->forward(StreamEnd::make());
      session->close();
      session->detach();
    }
  }
}

void MuxSessionPool::on_weak_ptr_gone() {
  m_weak_ptr_gone = true;
  m_map->m_weak_pools.erase(m_weak_key);
  schedule_recycling();
}

//
// MuxSessionMap
//
// Referenced and retained by:
//   - MuxSources
//   - MuxSessionPool
//   - Asynchronous recycling operations
//

auto MuxSessionMap::alloc(const pjs::Value &key, MuxSource *source) -> MuxSession* {
  auto i = m_pools.find(key);
  if (i != m_pools.end()) {
    return i->second->alloc();
  }

  auto pool = source->on_mux_new_pool();
  if (!pool) return nullptr;

  pool->m_map = this;
  pool->m_key = key;
  m_pools[key] = pool;

  return pool->alloc();
}

auto MuxSessionMap::alloc(pjs::Object::WeakPtr *weak_key, MuxSource *source) -> MuxSession* {
  auto i = m_weak_pools.find(weak_key);
  if (i != m_weak_pools.end()) {
    return i->second->alloc();
  }

  auto pool = source->on_mux_new_pool();
  if (!pool) return nullptr;

  pool->m_map = this;
  pool->m_weak_key = weak_key;
  pool->watch(weak_key);
  m_weak_pools[weak_key] = pool;

  return pool->alloc();
}

void MuxSessionMap::schedule_recycling() {
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
// MuxSource
//

MuxSource::MuxSource(std::shared_ptr<BufferStats> buffer_stats)
  : m_map(new MuxSessionMap())
  , m_waiting_events(buffer_stats)
{
}

MuxSource::MuxSource(const MuxSource &r)
  : m_map(r.m_map)
  , m_waiting_events(r.m_waiting_events)
{
}

void MuxSource::reset() {
  if (m_session) {
    stop_waiting();
    close_stream();
    m_session = nullptr;
  }
  m_waiting_events.clear();
  m_session_key = pjs::Value::undefined;
  m_session_weak_key = nullptr;
  m_has_alloc_error = false;
}

void MuxSource::discard() {
  m_stream = nullptr;
}

void MuxSource::key(const pjs::Value &key) {
  if (key.is_object() && key.o()) {
    m_session_weak_key = key.o()->weak_ptr();
  } else {
    m_session_key = key;
  }
}

void MuxSource::chain(EventTarget::Input *input) {
  m_output = input;
  if (m_stream) {
    m_stream->chain(input);
  }
}

void MuxSource::input(Event *evt) {
  alloc_stream();

  if (m_is_waiting) {
    m_waiting_events.push(evt);

  } else if (auto s = m_stream) {
    auto i = s->input();
    MuxSession::auto_release(m_session);
    if (!m_waiting_events.empty()) {
      m_waiting_events.flush(
        [=](Event *evt) {
          i->input(evt);
        }
      );
    }
    i->input(evt);
  }
}

void MuxSource::alloc_stream() {
  if (m_session && !m_session->is_open()) {
    stop_waiting();
    close_stream();
    m_session = nullptr;
  }

  if (!m_stream && !m_has_alloc_error) {
    auto session = m_session.get();
    if (!session) {
      session = (
        m_session_weak_key ?
          m_map->alloc(m_session_weak_key, this) :
          m_map->alloc(m_session_key, this)
      );
      if (!session) {
        m_has_alloc_error = true;
        return;
      }
      m_session = session;
    }

    if (!session->is_open()) {
      session->open(this, on_mux_new_pipeline()); // might've got EOS after
      if (auto eos = session->m_eos.get()) {
        m_session = nullptr;
        m_output->input(eos);
        return;
      }
    }

    if (session->is_pending()) {
      start_waiting();
      return;
    }

    auto s = m_session->mux_session_open_stream(this);
    s->chain(m_output);
    m_stream = s;
  }
}

void MuxSource::start_waiting() {
  if (!m_is_waiting) {
    m_session->m_waiting_sources.push(this);
    m_is_waiting = true;
  }
}

void MuxSource::flush_waiting() {
  stop_waiting();
  Net::current().post(
    [this]() {
      InputContext ic;
      MuxSource::input(Data::make());
    }
  );
}

void MuxSource::stop_waiting() {
  if (m_is_waiting) {
    m_session->m_waiting_sources.remove(this);
    m_is_waiting = false;
  }
}

void MuxSource::close_stream() {
  if (auto *s = m_stream) {
    s->chain(nullptr);
    m_session->mux_session_close_stream(s);
    m_stream = nullptr;
  }
}

//
// MuxQueue
//

MuxQueue::~MuxQueue() {
  m_session = nullptr; // So that session destruction will recur as stream destruction
}

void MuxQueue::reset() {
  EventSource::close();
  while (auto r = m_receivers.head()) {
    m_receivers.remove(r);
    delete r;
  }
  m_dedicated_stream = nullptr;
}

void MuxQueue::increase_output_count(int n) {
  if (auto r = m_receivers.head()) {
    r->increase_output_count(n);
  }
}

void MuxQueue::dedicate() {
  if (auto r = m_receivers.head()) {
    m_dedicated_stream = r->stream();
    while (auto r = m_receivers.head()) {
      m_receivers.remove(r);
      delete r;
    }
  }
}

auto MuxQueue::stream(MuxSource *source) -> EventFunction* {
  auto s = new Stream(this, source);
  s->retain();
  return s;
}

void MuxQueue::close(EventFunction *stream) {
  auto s = static_cast<Stream*>(stream);
  s->release();
}

void MuxQueue::on_reply(Event *evt) {
  if (auto s = m_dedicated_stream.get()) {
    s->output(evt);
    if (auto eos = evt->as<StreamEnd>()) {
      on_queue_end(eos);
    }

  } else if (auto r = m_receivers.head()) {
    if (r->receive(evt)) {
      m_receivers.remove(r);
      delete r;
    }
    if (auto eos = evt->as<StreamEnd>()) {
      for (auto r = m_receivers.head(); r; r = r->next()) {
        auto s = r->stream();
        s->output(evt->clone());
      }
      reset();
      on_queue_end(eos);
    }

  } else if (auto eos = evt->as<StreamEnd>()) {
    on_queue_end(eos);
  }
}

//
// MuxQueue::Stream
//
// Referenced and retained by:
//   - MuxSource, via mux_session_open_stream() and mux_session_close_stream()
//   - MuxQueue if dedicated
//   - MuxQueue::Receiver
//

MuxQueue::Stream::Stream(MuxQueue *queue, MuxSource *source)
  : m_queue(queue)
  , m_source(source)
{
  queue->m_session->increase_share_count();
}

MuxQueue::Stream::~Stream() {
  if (m_queue->m_session) {
    m_queue->m_session->decrease_share_count();
  }
}

void MuxQueue::Stream::on_event(Event *evt) {
  auto q = m_queue;

  if (auto s = q->m_dedicated_stream.get()) {
    if (s == this) q->output(evt);
    return;
  }

  switch (evt->type()) {
    case Event::Type::MessageStart:
      if (!m_message_start) {
        m_message_start = evt->as<MessageStart>();
        m_buffer.clear();
      }
      break;
    case Event::Type::Data:
      if (m_message_start) {
        m_buffer.push(*evt->as<Data>());
      }
      break;
    case Event::Type::MessageEnd:
      if (m_message_start) {
        int n = q->on_queue_message(m_source, m_message_start);
        if (n >= 0) {
          if (n > 0) {
            auto r = new Receiver(this, n);
            q->m_receivers.push(r);
            m_receiver_count++;
          }
          auto i = q->output();
          i->input(m_message_start);
          if (!m_buffer.empty()) i->input(Data::make(std::move(m_buffer)));
          i->input(evt);
        }
        m_message_start = nullptr;
      }
      break;
    case Event::Type::StreamEnd:
      m_message_start = nullptr;
      if (m_receiver_count > 0) {
        m_eos = evt->as<StreamEnd>();
      } else {
        EventFunction::output(evt);
      }
      break;
  }
}

void MuxQueue::Stream::shift() {
  if (!--m_receiver_count) {
    if (m_eos) {
      EventFunction::output(m_eos);
      m_eos = nullptr;
    }
  }
}

//
// MuxQueue::Receiver
//

bool MuxQueue::Receiver::receive(Event *evt) {
  switch (evt->type()) {
    case Event::Type::MessageStart:
      if (!m_has_message_started) {
        m_stream->output(evt);
        m_has_message_started = true;
      }
      break;
    case Event::Type::Data:
      if (m_has_message_started) {
        m_stream->output(evt);
      }
      break;
    case Event::Type::MessageEnd:
    case Event::Type::StreamEnd:
      if (m_has_message_started) {
        m_stream->output(evt->is<StreamEnd>() ? MessageEnd::make() : evt);
        m_has_message_started = false;
        if (!--m_output_count) {
          m_stream->shift();
          return true;
        }
      }
      break;
  }
  return false;
}

//
// MuxBase
//

MuxBase::MuxBase()
  : MuxSource(Filter::buffer_stats())
{
}

MuxBase::MuxBase(const MuxBase &r)
  : Filter(r)
  , MuxSource(r)
  , m_session_selector(r.m_session_selector)
  , m_options(r.m_options)
{
}

MuxBase::MuxBase(pjs::Function *session_selector)
  : m_session_selector(session_selector)
{
}

MuxBase::MuxBase(pjs::Function *session_selector, pjs::Function *options)
  : m_session_selector(session_selector)
  , m_options(options)
{
}

void MuxBase::reset() {
  Filter::reset();
  MuxSource::reset();
  m_session_key_ready = false;
}

void MuxBase::chain() {
  Filter::chain();
  MuxSource::chain(Filter::output());
}

void MuxBase::shutdown() {
  Filter::shutdown();
  if (auto map = MuxSource::map()) {
    map->shutdown();
  }
}

void MuxBase::process(Event *evt) {
  if (!m_session_key_ready) {
    m_session_key_ready = true;
    pjs::Value key;
    if (m_session_selector && !Filter::eval(m_session_selector, key)) return;
    if (key.is_undefined()) key.set(Filter::context()->inbound());
    MuxSource::key(key);
  }

  MuxSource::input(evt);
}

auto MuxBase::on_mux_new_pool() -> MuxSessionPool* {
  if (auto f = m_options.get()) {
    pjs::Value opts;
    if (!Filter::eval(f, opts)) return nullptr;
    if (!opts.is_object()) {
      Filter::error("callback did not return an object for options");
      return nullptr;
    }
    return on_mux_new_pool(opts.o());
  } else {
    return on_mux_new_pool(nullptr);
  }
}

auto MuxBase::on_mux_new_pipeline() -> Pipeline* {
  return sub_pipeline(0, false);
}

//
// Mux::Options
//

Mux::Options::Options(pjs::Object *options)
  : MuxSession::Options(options)
{
  Value(options, "outputCount")
    .get(output_count)
    .get(output_count_f)
    .check_nullable();
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(pjs::Function *session_selector)
  : MuxBase(session_selector)
{
}

Mux::Mux(pjs::Function *session_selector, const Options &options)
  : MuxBase(session_selector)
  , m_options(options)
{
}

Mux::Mux(pjs::Function *session_selector, pjs::Function *options)
  : MuxBase(session_selector, options)
{
}

Mux::Mux(const Mux &r)
  : MuxBase(r)
  , m_options(r.m_options)
{
}

Mux::~Mux()
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

auto Mux::on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* {
  if (options) {
    try {
      Options opts(options);
      return new SessionPool(opts);
    } catch (std::runtime_error &err) {
      Filter::error(err.what());
      return nullptr;
    }
  } else {
    return new SessionPool(m_options);
  }
}

//
// Mux::Session
//

void Mux::Session::mux_session_open(MuxSource *source) {
  MuxQueue::chain(MuxSession::input());
  MuxSession::chain(MuxQueue::reply());
}

auto Mux::Session::mux_session_open_stream(MuxSource *source) -> EventFunction* {
  return MuxQueue::stream(source);
}

void Mux::Session::mux_session_close_stream(EventFunction *stream) {
  MuxQueue::close(stream);
}

void Mux::Session::mux_session_close() {
  MuxQueue::reset();
}

auto Mux::Session::on_queue_message(MuxSource *source, MessageStart *msg) -> int {
  auto pool = static_cast<SessionPool*>(MuxSession::pool());
  if (auto f = pool->m_output_count_f.get()) {
    auto mux = static_cast<Mux*>(source);
    pjs::Value arg(msg), ret;
    if (!mux->Filter::callback(f, 1, &arg, ret)) return 1;
    return ret.to_int32();
  } else {
    return pool->m_output_count;
  }
}

void Mux::Session::on_queue_end(StreamEnd *eos) {
  MuxSession::end(eos);
}

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

} // namespace pipy
