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

#include "api/console.hpp"

#include <limits>

namespace pipy {

//
// Muxer::Options
//

Muxer::Options::Options(pjs::Object *options) {
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
// Muxer
//
// Manages one single stream (as an EventFunction), which is allocated from a Session
// with a given session key, which is allocated from the SessionPool.
//
// +-------------+       +----------------+       +---------+       +------------------------+
// |             | 1   * |                | 1   * |         | 1   * |                        |
// | SessionPool |------>| SessionCluster |------>| Session |------>| Stream (EventFunction) |
// |             |       |                |       |         |       |                        |
// +-------------+       +----------------+       +---------+       +------------------------+
//
// After creation, a Session can be in "pending mode" due to some asynchronous
// handshaking going on in the Session's pipeline, such as TLS handshake before
// the actual protocol is selected by ALPN.
//

Muxer::Muxer()
  : m_session_pool(new SessionPool())
{
}

Muxer::Muxer(const Muxer &r)
  : m_session_pool(r.m_session_pool)
{
}

void Muxer::reset() {
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
  m_session_key = pjs::Value::undefined;
}

void Muxer::shutdown() {
  m_session_pool->shutdown();
}

void Muxer::open(EventTarget::Input *output) {
  if (!m_stream) {
    auto session = m_session.get();
    if (!session) {
      if (!on_select_session(m_session_key)) return;
      session = m_session_pool->alloc(this, m_session_key);
      if (!session) return;
      m_session = session;
    }

    if (!session->m_pipeline) {
      pjs::Ref<SessionInfo> si = SessionInfo::make();
      si->sessionKey = m_session_key;
      si->sessionCount = session->m_cluster->m_sessions.size();
      auto p = on_new_pipeline(session->reply(), si);
      session->link(this, p);
    }

    if (session->is_pending()) {
      start_waiting();
      return;
    }

    auto s = m_session->open_stream(this);
    s->chain(output);
    m_stream = s;
  }
}

void Muxer::write(Event *evt) {
  if (m_stream) {
    m_stream->input()->input(evt);
  }
}

void Muxer::start_waiting() {
  if (!m_waiting) {
    m_session->m_waiting_muxers.push(this);
    m_waiting = true;
  }
}

void Muxer::flush_waiting() {
  stop_waiting();
  on_pending_session_open();
}

void Muxer::stop_waiting() {
  if (m_waiting) {
    m_session->m_waiting_muxers.remove(this);
    m_waiting = false;
  }
}

//
// Muxer::Session
//
// Construction:
//   - When a new session key is requested by Muxer
//     Call path:
//       alloc()
//       link()
//         open() override
//
// Destruction:
//   - When share count reaches 0, and
//       a) Been for a while of maxIdle, or
//       b) Seen a StreamEnd coming out from the pipeline, or
//       c) A weak session key is gone
//     Call path:
//       recycle()
//         unlink()
//           close() override
//         detach()
//   - When a detached session is freed by Muxer
//     Call path:
//       free()
//         unlink()
//           close() override
//
// Session owns streams:
//   - Session::open_stream(): Creates a new stream
//   - Session::close_stream(): Destroys an existing stream
//

void Muxer::Session::detach() {
  if (auto cluster = m_cluster) {
    m_cluster = nullptr;
    cluster->discard(this);
  }
}

void Muxer::Session::set_pending(bool pending) {
  if (pending != m_is_pending) {
    if (!pending) {
      for (auto *p = m_waiting_muxers.head(); p; ) {
        auto *muxer = p; p = p->List<Muxer>::Item::next();
        muxer->flush_waiting();
      }
    }
    m_is_pending = pending;
  }
}

void Muxer::Session::link(Muxer *muxer, Pipeline *pipeline) {
  m_pipeline = pipeline;
  chain_forward(pipeline->input());
  open(muxer);
}

void Muxer::Session::unlink(bool forward) {
  if (auto p = m_pipeline.get()) {
    close();
    if (forward) EventProxy::forward(StreamEnd::make());
    Pipeline::auto_release(p);
    m_pipeline = nullptr;
  }
}

void Muxer::Session::free() {
  if (m_cluster) {
    m_cluster->free(this);
  } else {
    unlink(true);
  }
}

void Muxer::Session::on_input(Event *evt) {
  forward(evt);
}

void Muxer::Session::on_reply(Event *evt) {
  if (evt->is<StreamEnd>()) {
    output(evt);
    unlink(false);
    detach();
  } else {
    output(evt);
  }
}

//
// Muxer::SessionCluster
//
// This is the container for all Sessions with the same session key.
//

Muxer::SessionCluster::SessionCluster(const Options &options) {
  m_max_idle = options.max_idle;
  m_max_queue = options.max_queue;
  m_max_messages = options.max_messages;
}

auto Muxer::SessionCluster::alloc() -> Session* {
  auto max_share_count = m_max_queue;
  auto max_message_count = m_max_messages;
  auto *s = m_sessions.head();
  while (s) {
    if ((max_share_count <= 0 || s->m_share_count < max_share_count) &&
        (max_message_count <= 0 || s->m_message_count < max_message_count)
      ) {
      s->m_share_count++;
      s->m_message_count++;
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

void Muxer::SessionCluster::free(Session *session) {
  session->m_share_count--;
  if (session->is_free()) {
    session->m_free_time = utils::now();
  }
  sort(session);
}

void Muxer::SessionCluster::discard(Session *session) {
  Session::auto_release(session);
  m_sessions.remove(session);
  session->release();
  sort(nullptr);
}

void Muxer::SessionCluster::sort(Session *session) {
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
      m_pool->m_weak_clusters.erase(m_weak_key);
    } else {
      m_pool->m_clusters.erase(m_key);
    }
    free();
  }
}

void Muxer::SessionCluster::schedule_recycling() {
  auto s = m_sessions.head();
  if (!s || s->m_share_count > 0) {
    if (m_recycle_scheduled) {
      m_pool->m_recycle_clusters.remove(this);
      m_recycle_scheduled = false;
    }
  } else {
    if (!m_recycle_scheduled) {
      m_pool->m_recycle_clusters.push(this);
      m_pool->recycle();
      m_recycle_scheduled = true;
    }
  }
}

void Muxer::SessionCluster::recycle(double now) {
  auto max_idle = m_max_idle * 1000;
  auto s = m_sessions.head();
  while (s) {
    auto session = s; s = s->next();
    if (session->m_share_count > 0) break;
    if (session->m_is_pending || m_weak_ptr_gone ||
       (m_max_messages > 0 && session->m_message_count >= m_max_messages) ||
       (now - session->m_free_time >= max_idle))
    {
      session->unlink(true);
      session->detach();
    }
  }
}

void Muxer::SessionCluster::on_weak_ptr_gone() {
  m_weak_ptr_gone = true;
  m_pool->m_weak_clusters.erase(m_weak_key);
  schedule_recycling();
}

//
// Muxer::SessionPool
//

Muxer::SessionPool::~SessionPool() {
  for (const auto &p : m_clusters) p.second->free();
  for (const auto &p : m_weak_clusters) p.second->free();
}

auto Muxer::SessionPool::alloc(Muxer *mux, const pjs::Value &key) -> Session* {
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

  cluster = mux->on_new_cluster();
  if (!cluster) return nullptr;

  cluster->m_pool = this;

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

void Muxer::SessionPool::shutdown() {
  m_has_shutdown = true;
}

void Muxer::SessionPool::recycle() {
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
// Muxer::Queue
//

void Muxer::Queue::reset() {
  while (auto r = m_receivers.head()) {
    m_receivers.remove(r);
    delete r;
  }
  m_dedicated_stream = nullptr;
}

auto Muxer::Queue::open_stream(Muxer *muxer) -> EventFunction* {
  auto s = new Stream(muxer, this);
  s->retain();
  return s;
}

void Muxer::Queue::close_stream(EventFunction *stream) {
  auto s = static_cast<Stream*>(stream);
  s->release();
}

void Muxer::Queue::increase_queue_count() {
  if (auto r = m_receivers.head()) {
    r->increase_output_count(1);
  }
}

void Muxer::Queue::dedicate() {
  if (auto s = m_receivers.head()) {
    m_dedicated_stream = s->stream();
    while (auto r = m_receivers.head()) {
      m_receivers.remove(r);
      delete r;
    }
  }
}

void Muxer::Queue::on_reply(Event *evt) {
  if (auto s = m_dedicated_stream.get()) {
    s->output(evt);
  } else if (auto r = m_receivers.head()) {
    if (r->receive(evt)) {
      m_receivers.remove(r);
      delete r;
    }
  }
}

//
// Muxer::Queue::Stream
//
// Retain:
//   - Session::open_stream()
//   - After queued
// Release:
//   - Session::close_stream()
//   - After replied
//

void Muxer::Queue::Stream::on_event(Event *evt) {
  auto queue = m_queue;

  if (auto s = queue->m_dedicated_stream.get()) {
    if (s == this) queue->output(evt);
    return;
  }

  if (auto msg = m_reader.read(evt)) {
    int n = queue->on_queue_message(m_muxer, msg);
    if (n >= 0) {
      if (n > 0) {
        auto r = new Receiver(this, n);
        queue->m_receivers.push(r);
        m_receiver_count++;
      }
      msg->write(queue->output());
    }
    msg->release();
  }

  if (auto end = evt->as<StreamEnd>()) {
    if (m_receiver_count > 0) {
      m_stream_end = end;
    } else {
      EventFunction::output(evt);
    }
  }
}

void Muxer::Queue::Stream::shift() {
  if (!--m_receiver_count) {
    if (auto end = m_stream_end.get()) {
      EventFunction::output(end);
    }
  }
}

//
// Muxer::Queue::Receiver
//

bool Muxer::Queue::Receiver::receive(Event *evt) {
  switch (evt->type()) {
    case Event::Type::MessageStart:
      if (!m_message_started) {
        m_stream->output(evt);
        m_message_started = true;
      }
      break;
    case Event::Type::Data:
      if (m_message_started) {
        m_stream->output(evt);
      }
      break;
    case Event::Type::MessageEnd:
      if (m_message_started) {
        m_stream->output(evt);
        m_message_started = false;
        if (!--m_output_count) {
          m_stream->shift();
          return true;
        }
      }
      break;
    case Event::Type::StreamEnd:
      if (m_message_started && m_output_count == 1) {
        m_stream->output(MessageEnd::make(evt->as<StreamEnd>()));
        m_message_started = false;
        if (!--m_output_count) {
          m_stream->shift();
          return true;
        }
      } else {
        m_stream->output(evt);
        m_stream->m_stream_end = nullptr;
      }
      break;
  }
  return false;
}

//
// MuxBase
//

MuxBase::MuxBase()
{
}

MuxBase::MuxBase(const MuxBase &r)
  : Filter(r)
  , Muxer(r)
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
  Muxer::reset();
  m_waiting_events.clear();
}

void MuxBase::process(Event *evt) {
  Muxer::open(Filter::output());
  if (Muxer::stream()) {
    if (!m_waiting_events.empty()) {
      m_waiting_events.flush(
        [this](Event *evt) {
          Muxer::write(evt);
        }
      );
    }
    Muxer::write(evt);
  } else {
    m_waiting_events.push(evt);
  }
}

bool MuxBase::on_select_session(pjs::Value &key) {
  if (m_session_selector && !Filter::eval(m_session_selector, key)) return false;
  if (key.is_undefined()) key.set(Filter::context()->inbound());
  return true;
}

auto MuxBase::on_new_cluster() -> MuxBase::SessionCluster* {
  if (auto f = m_options.get()) {
    pjs::Value opts;
    if (!Filter::eval(f, opts)) return nullptr;
    if (!opts.is_object()) {
      Filter::error("callback did not return an object for options");
      return nullptr;
    }
    return on_new_cluster(opts.o());
  } else {
    return on_new_cluster(nullptr);
  }
}

auto MuxBase::on_new_pipeline(EventTarget::Input *output, pjs::Object *session_info) -> Pipeline* {
  pjs::Value arg(session_info);
  return Filter::sub_pipeline(0, false, output, nullptr, 1, &arg);
}

void MuxBase::on_pending_session_open() {
  Muxer::open(Filter::output());
  Filter::input()->flush_async();
}

//
// Mux::Options
//

Mux::Options::Options(pjs::Object *options)
  : MuxBase::Options(options)
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

Mux::~Mux() {
}

void Mux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "mux";
  d.sub_type = Dump::MUX;
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* {
  if (options) {
    try {
      Options opts(options);
      return new SessionCluster(opts);
    } catch (std::runtime_error &err) {
      Filter::error(err.what());
      return nullptr;
    }
  } else {
    return new SessionCluster(m_options);
  }
}

//
// Mux::Session
//

void Mux::Session::open(Muxer *muxer) {
  Muxer::Queue::chain(MuxBase::Session::input());
  MuxBase::Session::chain(Muxer::Queue::reply());
}

auto Mux::Session::open_stream(Muxer *muxer) -> EventFunction* {
  return Muxer::Queue::open_stream(muxer);
}

void Mux::Session::close_stream(EventFunction *stream) {
  return Muxer::Queue::close_stream(stream);
}

void Mux::Session::close() {
  Muxer::Queue::reset();
}

auto Mux::Session::on_queue_message(Muxer *muxer, Message *msg) -> int {
  auto cluster = static_cast<SessionCluster*>(Mux::Session::cluster());
  if (auto f = cluster->m_output_count_f.get()) {
    auto mux = static_cast<Mux*>(muxer);
    pjs::Value arg(msg), ret;
    if (!mux->Filter::callback(f, 1, &arg, ret)) {
      close();
      return -1;
    }
    return ret.to_int32();
  } else {
    return cluster->m_output_count;
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Muxer::SessionInfo>::init() {
  field<Value>("sessionKey", [](Muxer::SessionInfo *obj) { return &obj->sessionKey; });
  field<int>("sessionCount", [](Muxer::SessionInfo *obj) { return &obj->sessionCount; });
}

} // namespace pjs
