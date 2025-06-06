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

#ifndef MUX_HPP
#define MUX_HPP

#include "filter.hpp"
#include "data.hpp"
#include "event.hpp"
#include "input.hpp"
#include "pipeline.hpp"
#include "list.hpp"
#include "timer.hpp"
#include "options.hpp"
#include "utils.hpp"

#include <unordered_map>

namespace pipy {

class MuxSession;
class MuxSessionPool;
class MuxSessionMap;
class MuxSource;

//
// MuxSession
//

class MuxSession :
  public AutoReleased,
  public EventProxy,
  public List<MuxSession>::Item
{
public:

  //
  // MuxSession::Options
  //

  struct Options : public pipy::Options {
    double max_idle = 60;
    int max_queue = 1;
    int max_messages = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  virtual void mux_session_open(MuxSource *source) = 0;
  virtual auto mux_session_open_stream(MuxSource *source) -> EventFunction* = 0;
  virtual void mux_session_close_stream(EventFunction *stream) = 0;
  virtual void mux_session_close() = 0;

  void increase_share_count();
  void decrease_share_count();

protected:
  auto pool() const -> MuxSessionPool* { return m_pool; }
  auto input() const -> EventTarget::Input* { return m_pipeline->input(); }
  bool is_open() const { return m_pipeline; }
  bool is_free() const { return !m_share_count; }
  bool is_pending() const { return m_is_pending; }
  void set_pending(bool pending);
  void detach();
  void end(StreamEnd *eos);

private:
  void open(MuxSource *source, Pipeline *pipeline);
  void close();

  MuxSessionPool* m_pool = nullptr;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<StreamEnd> m_eos;
  List<MuxSource> m_waiting_sources;
  int m_share_count = 0;
  int m_message_count = 0;
  double m_free_time = 0;
  bool m_is_pending = false;

  virtual void on_reply(Event *evt) override {
    MuxSession::auto_release(this);
    EventProxy::output(evt);
  }

  friend class pjs::RefCount<MuxSession>;
  friend class MuxSource;
  friend class MuxSessionPool;
};

//
// MuxSessionPool
//

class MuxSessionPool :
  public pjs::Object::WeakPtr::Watcher,
  public List<MuxSessionPool>::Item
{
protected:
  MuxSessionPool(const MuxSession::Options &options);

  virtual auto session() -> MuxSession* = 0;
  virtual void free() = 0;

private:
  auto alloc() -> MuxSession*;
  void free(MuxSession *session);
  void detach(MuxSession *session);

  pjs::Value m_key;
  pjs::Ref<pjs::Object::WeakPtr> m_weak_key;
  pjs::Ref<MuxSessionMap> m_map;
  List<MuxSession> m_sessions;
  double m_max_idle;
  int m_max_queue;
  int m_max_messages;
  bool m_weak_ptr_gone = false;
  bool m_recycle_scheduled = false;

  void sort(MuxSession *session);
  void schedule_recycling();
  void recycle(double now);

  virtual void on_weak_ptr_gone() override;

  friend class MuxSource;
  friend class MuxSession;
  friend class MuxSessionMap;
};

//
// MuxSessionMap
//

class MuxSessionMap : public pjs::RefCount<MuxSessionMap> {
public:
  void shutdown() { m_has_shutdown = true; }

private:
  std::unordered_map<pjs::Value, MuxSessionPool*> m_pools;
  std::unordered_map<pjs::Ref<pjs::Object::WeakPtr>, MuxSessionPool*> m_weak_pools;
  List<MuxSessionPool> m_recycle_pools;
  Timer m_recycle_timer;
  bool m_has_recycling_scheduled = false;
  bool m_has_shutdown = false;

  auto alloc(const pjs::Value &key, MuxSource *source) -> MuxSession*;
  auto alloc(pjs::Object::WeakPtr *weak_key, MuxSource *source) -> MuxSession*;
  void schedule_recycling();

  friend class pjs::RefCount<MuxSessionMap>;
  friend class MuxSource;
  friend class MuxSessionPool;
};

//
// MuxSource
//

class MuxSource : public List<MuxSource>::Item {
public:
  void discard();

protected:
  MuxSource(std::shared_ptr<BufferStats> buffer_stats = nullptr);
  MuxSource(const MuxSource &r);

  void reset();
  void key(const pjs::Value &key);
  auto map() -> MuxSessionMap* { return m_map; }

  void chain(EventTarget::Input *input);
  void input(Event *evt);

  virtual auto on_mux_new_pool() -> MuxSessionPool* = 0;
  virtual auto on_mux_new_pipeline() -> Pipeline* = 0;

private:
  pjs::Ref<MuxSessionMap> m_map;
  pjs::Ref<MuxSession> m_session;
  pjs::Value m_session_key;
  pjs::Ref<pjs::Object::WeakPtr> m_session_weak_key;
  pjs::Ref<EventTarget::Input> m_output;
  EventFunction* m_stream = nullptr;
  EventBuffer m_waiting_events;
  bool m_is_waiting = false;
  bool m_has_alloc_error = false;

  void alloc_stream();
  void start_waiting();
  void flush_waiting();
  void stop_waiting();
  void close_stream();

  friend class MuxSession;
  friend class MuxSessionMap;
};

//
// MuxQueue
//

class MuxQueue : public EventSource {
protected:
  MuxQueue(MuxSession *session) : m_session(session) {}
  ~MuxQueue();

  void reset();
  auto stream(MuxSource *source) -> EventFunction*;
  void close(EventFunction *stream);
  void increase_output_count(int n);
  void dedicate();

  virtual auto on_queue_message(MuxSource *source, MessageStart *msg) -> int { return 1; }
  virtual void on_queue_end(StreamEnd *eos) {}

private:
  void on_reply(Event *evt) override;

  //
  // MuxQueue::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public pjs::RefCount<Stream>,
    public EventFunction
  {
  public:
    Stream(MuxQueue *queue, MuxSource *source);
    ~Stream();

  protected:
    virtual void on_event(Event *evt) override;

  private:
    MuxQueue* m_queue;
    MuxSource* m_source;
    pjs::Ref<MessageStart> m_message_start;
    pjs::Ref<StreamEnd> m_eos;
    Data m_buffer;
    int m_receiver_count = 0;

    void shift();

    friend class MuxQueue;
  };

  //
  // MuxQueue::Receiver
  //

  class Receiver :
    public pjs::Pooled<Receiver>,
    public List<Receiver>::Item
  {
  public:
    Receiver(Stream *stream, int output_count)
      : m_stream(stream)
      , m_output_count(output_count) {}

    auto stream() const -> Stream* { return m_stream; }
    void increase_output_count(int n) { m_output_count += n; }
    bool receive(Event *evt);

  private:
    pjs::Ref<Stream> m_stream;
    int m_output_count;
    bool m_has_message_started = false;

    friend class MuxQueue;
  };

  MuxSession* m_session;
  List<Receiver> m_receivers;
  pjs::Ref<Stream> m_dedicated_stream;
};

//
// MuxBase
//

class MuxBase : public Filter, public MuxSource {
protected:
  MuxBase();
  MuxBase(const MuxBase &r);
  MuxBase(pjs::Function *session_selector);
  MuxBase(pjs::Function *session_selector, pjs::Function *options);

  virtual void reset() override;
  virtual void chain() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;

  virtual auto on_mux_new_pool() -> MuxSessionPool* override;
  virtual auto on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* = 0;
  virtual auto on_mux_new_pipeline() -> Pipeline* override;

  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Ref<pjs::Function> m_options;
  bool m_session_key_ready = false;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  struct Options : public MuxSession::Options {
    int output_count = 1;
    pjs::Ref<pjs::Function> output_count_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Mux();
  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const Options &options);
  Mux(pjs::Function *session_selector, pjs::Function *options);

protected:
  Mux(const Mux &r);
  ~Mux();

  virtual void dump(Dump &d) override;
  virtual auto clone() -> Filter* override;
  virtual auto on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* override;

  //
  // Mux::Session
  //

  struct Session :
    public pjs::Pooled<Session, MuxSession>,
    public MuxQueue
  {
    Session() : MuxQueue(this) {}

    virtual void mux_session_open(MuxSource *source) override;
    virtual auto mux_session_open_stream(MuxSource *source) -> EventFunction* override;
    virtual void mux_session_close_stream(EventFunction *stream) override;
    virtual void mux_session_close() override;
    virtual auto on_queue_message(MuxSource *source, MessageStart *msg) -> int override;
    virtual void on_queue_end(StreamEnd *eos) override;
    virtual void on_auto_release() override { delete this; }
  };

  //
  // Mux::SessionPool
  //

  struct SessionPool : public pjs::Pooled<SessionPool, MuxSessionPool> {
    SessionPool(const Options &options)
      : pjs::Pooled<SessionPool, MuxSessionPool>(options)
      , m_output_count(options.output_count)
      , m_output_count_f(options.output_count_f) {}

    virtual auto session() -> MuxSession* override { return new Session(); }
    virtual void free() override { delete this; }

    int m_output_count;
    pjs::Ref<pjs::Function> m_output_count_f;
  };

private:
  Options m_options;
};

//
// Muxer
//

class Muxer : public pjs::RefCount<Muxer> {
public:

  //
  // Muxer::Options
  //

  struct Options : public pipy::Options {
    double max_idle = 60;
    int max_sessions = 0;
    Options() {}
    Options(pjs::Object *options);
  };

private:
  class SessionPool;

public:
  class Session;

  //
  // Muxer::Stream
  //

  class Stream : public List<Stream>::Item {
  public:
    Stream() {}
    ~Stream() { if (m_session) m_session->m_streams.remove(this); }

    auto session() -> Session* { return m_session; }

  private:
    Session* m_session = nullptr;

    friend class Session;
  };

  //
  // Muxer::Session
  //

  class Session : public List<Session>::Item {
  protected:
    Session() : m_idle_time(utils::now()) {}
    ~Session() {
      if (m_pool) m_pool->m_sessions.remove(this);
      for (auto s = m_streams.head(); s; s = s->next()) s->m_session = nullptr;
    }

    void abort() { m_has_aborted = true; sort(); }

  public:
    auto head() const -> Stream* { return m_streams.head(); }
    auto tail() const -> Stream* { return m_streams.tail(); }
    void append(Stream *s) { m_streams.push(s); s->m_session = this; sort(); }
    void remove(Stream *s) { m_streams.remove(s); s->m_session = nullptr; sort(); }
    void allow_queuing(bool allowed) { m_allow_queuing = allowed; }

  private:
    SessionPool* m_pool = nullptr;
    List<Stream> m_streams;
    double m_idle_time;
    bool m_allow_queuing = false;
    bool m_has_aborted = false;

    bool is_idle() const { return m_streams.empty(); }

    bool is_idle_timeout(double now, double max_idle) const {
      return is_idle() && now - m_idle_time >= max_idle;
    }

    void sort() {
      if (m_pool) m_pool->sort(this);
      if (m_streams.empty()) {
        m_idle_time = utils::now();
      }
    }

    friend class SessionPool;
    friend class Stream;
  };

  Muxer() {}
  Muxer(const Options &options) : m_options(options) {}

  auto alloc(Filter *filter, const pjs::Value &key) -> Session*;
  void shutdown();

protected:
  virtual ~Muxer() {}

private:
  virtual auto on_muxer_session_open(Filter *filter) -> Session* = 0;
  virtual void on_muxer_session_close(Session *session) = 0;

  //
  // Muxer::SessionPool
  //

  class SessionPool :
    public pjs::Pooled<SessionPool>,
    public pjs::Object::WeakPtr::Watcher,
    public List<SessionPool>::Item
  {
  public:
    SessionPool(Muxer *muxer, const pjs::Value &key)
      : m_muxer(muxer)
      , m_key(key) {}

    SessionPool(Muxer *muxer, pjs::Object::WeakPtr *key)
      : m_muxer(muxer)
      , m_weak_key(key) { watch(key); }

    auto alloc(Filter *filter) -> Session*;
    void recycle(double now);

  private:
    pjs::Ref<Muxer> m_muxer;
    pjs::Ref<pjs::Object::WeakPtr> m_weak_key;
    pjs::Value m_key;
    List<Session> m_sessions;
    List<Session> m_aborted_sessions;
    bool m_weak_ptr_gone = false;
    bool m_has_recycling_scheduled = false;

    void sort(Session *session);
    void schedule_recycling();

    virtual void on_weak_ptr_gone() override;

    friend class Session;
  };

  Options m_options;
  std::unordered_map<pjs::Value, SessionPool*> m_pools;
  std::unordered_map<pjs::Ref<pjs::Object::WeakPtr>, SessionPool*> m_weak_pools;
  List<SessionPool> m_recycle_pools;
  Timer m_recycle_timer;
  bool m_has_recycling_scheduled = false;
  bool m_has_shutdown = false;

  void schedule_recycling();

  friend class pjs::RefCount<Muxer>;
};

} // namespace pipy

#endif // MUX_HPP
