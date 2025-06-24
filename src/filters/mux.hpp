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
#include "event.hpp"
#include "pipeline.hpp"
#include "context.hpp"
#include "list.hpp"
#include "timer.hpp"
#include "options.hpp"
#include "utils.hpp"

#include <unordered_map>

namespace pipy {

//
// Muxer
//

class Muxer : public pjs::RefCount<Muxer> {
public:

  //
  // Muxer::Options
  //

  struct Options : public pipy::Options {
    double max_idle = -1;
    int max_sessions = 0;
    Options() {}
    Options(pjs::Object *options);
  };

private:
  class SessionPool;

public:
  class Session;
  class Stream;

  //
  // Muxer::Stream
  //

  class Stream : public List<Stream>::Item {
  protected:
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

  public:
    auto head() const -> Stream* { return m_streams.head(); }
    auto tail() const -> Stream* { return m_streams.tail(); }
    void append(Stream *s) { m_streams.push(s); s->m_session = this; sort(); }
    void remove(Stream *s) { m_streams.remove(s); s->m_session = nullptr; sort(); }
    void allow_queuing(bool allowed) { m_allow_queuing = allowed; }
    void abort() { m_has_aborted = true; sort(); }

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

//
// Mux
//

class Mux : public Filter {
public:
  struct Options : public Muxer::Options {
    pjs::Ref<pjs::Function> message_key_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const Options &options);

private:
  Mux(const Mux &r);

  class Request;
  class Queue;
  class Pool;

  //
  // Mux::Request
  //

  class Request :
    public pjs::RefCount<Request>,
    public pjs::Pooled<Request>,
    public Muxer::Stream,
    public EventFunction
  {
  public:
    void discard();

  private:
    Request(const pjs::Value &key) : m_key(key) {}
    ~Request() {}

    virtual void on_event(Event *evt) override;

    pjs::Value m_key;
    EventBuffer m_buffer;
    bool m_is_sending = false;
    bool m_started = false;
    bool m_ended = false;

    friend class pjs::RefCount<Request>;
    friend class Queue;
  };

  //
  // Mux::Queue
  //

  class Queue :
    public pjs::RefCount<Queue>,
    public pjs::Pooled<Queue>,
    public Muxer::Session,
    public EventTarget
  {
  public:
    auto alloc(EventTarget::Input *output, const pjs::Value &key) -> Request*;

  private:
    Queue(Mux *mux);
    ~Queue() {}

    void free(Request *r);
    void free_all();

    pjs::Ref<Context> m_context;
    pjs::Ref<pjs::Function> m_message_key_f;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<Request> m_current_request;
    bool m_started = false;

    virtual void on_event(Event *evt) override;

    friend class pjs::RefCount<Queue>;
    friend class Request;
    friend class Pool;
  };

  //
  // Mux::Pool
  //

  class Pool : public Muxer {
  public:
    Pool();
    Pool(const Options &options);

  private:
    Options m_options;

    virtual auto on_muxer_session_open(Filter *filter) -> Session* override;
    virtual void on_muxer_session_close(Session *session) override;
  };

  pjs::Ref<Pool> m_pool;
  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Ref<Queue> m_queue;
  pjs::Ref<Request> m_request;
  Options m_options;
  bool m_has_error = false;

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
};

//
// MuxQueue
//

class MuxQueue : public Filter {
public:
  struct Options : public Muxer::Options {
    bool blocking = false;
    Options() {}
    Options(pjs::Object *options);
  };

  MuxQueue(pjs::Function *session_selector);
  MuxQueue(pjs::Function *session_selector, const Options &options);

private:
  MuxQueue(const MuxQueue &r);

  class Request;
  class Queue;
  class Pool;

  //
  // MuxQueue::Request
  //

  class Request :
    public pjs::RefCount<Request>,
    public pjs::Pooled<Request>,
    public Muxer::Stream,
    public EventFunction
  {
  public:
    void discard();

  private:
    Request() {}
    ~Request() {}

    virtual void on_event(Event *evt) override;

    EventBuffer m_buffer;
    bool m_is_sending = false;
    bool m_started = false;
    bool m_ended = false;

    friend class pjs::RefCount<Request>;
    friend class Queue;
  };

  //
  // MuxQueue::Queue
  //

  class Queue :
    public pjs::RefCount<Queue>,
    public pjs::Pooled<Queue>,
    public Muxer::Session,
    public EventTarget
  {
  public:
    auto alloc(EventTarget::Input *output) -> Request*;

  private:
    Queue(MuxQueue *mux);
    ~Queue() {}

    void free(Request *r);
    void free_all();

    pjs::Ref<Pipeline> m_pipeline;
    bool m_started = false;

    virtual void on_event(Event *evt) override;

    friend class pjs::RefCount<Queue>;
    friend class Request;
    friend class Pool;
  };

  //
  // MuxQueue::Pool
  //

  class Pool : public Muxer {
  public:
    Pool();
    Pool(const Options &options);

  private:
    Options m_options;

    virtual auto on_muxer_session_open(Filter *filter) -> Session* override;
    virtual void on_muxer_session_close(Session *session) override;
  };

  pjs::Ref<Pool> m_pool;
  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Ref<Queue> m_queue;
  pjs::Ref<Request> m_request;
  Options m_options;
  bool m_has_error = false;

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
};

} // namespace pipy

#endif // MUX_HPP
