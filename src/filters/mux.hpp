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
#include "message.hpp"
#include "input.hpp"
#include "context.hpp"
#include "list.hpp"
#include "timer.hpp"
#include "options.hpp"

#include <unordered_map>

namespace pipy {

class Data;

//
// Muxer
//

class Muxer : public List<Muxer>::Item {
public:
  struct Options : public pipy::Options {
    double max_idle = 60;
    int max_queue = 0;
    int max_messages = 0;
    Options() {}
    Options(pjs::Object *options);
  };

protected:
  class Session;
  class SessionCluster;
  class SessionPool;

  Muxer();
  Muxer(const Muxer &r);

  auto session() -> Session* { return m_session; }
  auto stream() -> EventFunction* { return m_stream; }

  void reset();
  void shutdown();
  void open(EventTarget::Input *output);
  void write(Event *evt);

  virtual bool on_select_session(pjs::Value &key) = 0;
  virtual auto on_new_cluster() -> SessionCluster* = 0;
  virtual auto on_new_pipeline(EventTarget::Input *output, pjs::Value args[2]) -> Pipeline* = 0;
  virtual void on_pending_session_open() {}

private:
  pjs::Ref<SessionPool> m_session_pool;
  pjs::Ref<Session> m_session;
  pjs::Value m_session_key;
  EventFunction* m_stream = nullptr;
  EventBuffer m_waiting_events;
  bool m_waiting = false;

  void start_waiting();
  void flush_waiting();
  void stop_waiting();

protected:

  //
  // Muxer::Session
  //

  class Session :
    public pjs::Pooled<Session>,
    public List<Session>::Item,
    public AutoReleased,
    public EventProxy
  {
  protected:
    virtual void open() = 0;
    virtual auto open_stream(Muxer *muxer) -> EventFunction* = 0;
    virtual void close_stream(EventFunction *stream) = 0;
    virtual void close() = 0;

    Session() {}
    virtual ~Session() {}

    auto cluster() const -> SessionCluster* { return m_cluster; }
    auto pipeline() const -> Pipeline* { return m_pipeline; }
    bool detached() const { return !m_cluster; }
    void detach();
    bool is_free() const { return !m_share_count; }
    bool is_pending() const { return m_is_pending; }
    void set_pending(bool pending);

  private:
    void link(Pipeline *pipeline);
    void unlink();
    void free();

    SessionCluster* m_cluster = nullptr;
    pjs::Ref<Pipeline> m_pipeline;
    int m_share_count = 1;
    int m_message_count = 0;
    double m_free_time = 0;
    List<Muxer> m_waiting_muxers;
    bool m_is_pending = false;
    bool m_is_closed = false;

    virtual void on_input(Event *evt) override;
    virtual void on_reply(Event *evt) override;
    virtual void on_recycle() override { delete this; }

    friend class pjs::RefCount<Session>;
    friend class Muxer;
  };

  //
  // Muxer::SessionCluter
  //

  class SessionCluster :
    public pjs::Pooled<SessionCluster>,
    public pjs::Object::WeakPtr::Watcher,
    public List<SessionCluster>::Item
  {
  protected:
    SessionCluster(const Options &options);
    virtual auto session() -> Session* = 0;
    virtual void free() = 0;

  private:
    auto alloc() -> Session*;
    void free(Session *session);
    void discard(Session *session);

    pjs::Ref<SessionPool> m_pool;
    pjs::Value m_key;
    pjs::WeakRef<pjs::Object> m_weak_key;
    List<Session> m_sessions;
    double m_max_idle;
    int m_max_queue;
    int m_max_messages;
    bool m_weak_ptr_gone = false;
    bool m_recycle_scheduled = false;

    void sort(Session *session);
    void schedule_recycling();
    void recycle(double now);

    virtual void on_weak_ptr_gone() override;

    friend class Muxer;
  };

  //
  // Muxer::SessionPool
  //

  class SessionPool : public pjs::RefCount<SessionPool> {
    ~SessionPool();

    auto alloc(Muxer *mux, const pjs::Value &key) -> Session*;
    void shutdown();

    std::unordered_map<pjs::Value, SessionCluster*> m_clusters;
    std::unordered_map<pjs::WeakRef<pjs::Object>, SessionCluster*> m_weak_clusters;
    List<SessionCluster> m_recycle_clusters;
    Timer m_recycle_timer;
    bool m_recycling = false;
    bool m_has_shutdown = false;

    void recycle();

    friend class pjs::RefCount<SessionPool>;
    friend class Muxer;
  };

  //
  // Muxer::Queue
  //

  class Queue : public EventSource {
  public:
    void reset();
    auto open_stream(Muxer *muxer) -> EventFunction*;
    void close_stream(EventFunction *stream);
    void increase_queue_count();
    void dedicate();

  protected:
    virtual auto on_queue_message(Muxer *muxer, Message *msg) -> int { return 1; }

  private:
    class Stream;
    class Receiver;

    List<Receiver> m_receivers;
    pjs::Ref<Stream> m_dedicated_stream;

    void on_reply(Event *evt) override;

    //
    // Muxer::Queue::Stream
    //

    class Stream :
      public pjs::Pooled<Stream>,
      public pjs::RefCount<Stream>,
      public EventFunction
    {
    protected:
      Stream(Muxer *muxer, Queue *queue)
        : m_muxer(muxer)
        , m_queue(queue) {}

      virtual void on_event(Event *evt) override;

    private:
      Muxer* m_muxer;
      Queue* m_queue;
      MessageReader m_reader;

      friend class Queue;
    };

    //
    // Muxer::Queue::Receiver
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
      MessageReader m_reader;
      int m_output_count;
    };

    friend class Stream;
  };

};

//
// MuxBase
//

class MuxBase : public Filter, public Muxer {
protected:
  MuxBase();
  MuxBase(pjs::Function *session_selector);
  MuxBase(pjs::Function *session_selector, pjs::Function *options);

  virtual void reset() override;
  virtual void process(Event *evt) override;

  virtual bool on_select_session(pjs::Value &key) override;
  virtual auto on_new_cluster() -> MuxBase::SessionCluster* override;
  virtual auto on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* = 0;
  virtual auto on_new_pipeline(EventTarget::Input *output, pjs::Value args[2]) -> Pipeline* override;
  virtual void on_pending_session_open() override;

  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Ref<pjs::Function> m_options;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  struct Options : public MuxBase::Options {
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

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;

  virtual auto on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* override;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxBase::Session>,
    public Muxer::Queue
  {
    virtual void open() override;
    virtual auto open_stream(Muxer *muxer) -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;
    virtual void close() override;
    virtual auto on_queue_message(Muxer *muxer, Message *msg) -> int override;

    friend class Mux;
  };

  //
  // Mux::SessionCluster
  //

  class SessionCluster : public pjs::Pooled<SessionCluster, MuxBase::SessionCluster> {
    SessionCluster(const Options &options)
      : pjs::Pooled<SessionCluster, MuxBase::SessionCluster>(options)
      , m_output_count(options.output_count)
      , m_output_count_f(options.output_count_f) {}

    virtual auto session() -> Session* override { return new Session(); }
    virtual void free() override { delete this; }

    int m_output_count;
    pjs::Ref<pjs::Function> m_output_count_f;

    friend class Mux;
  };

private:
  Options m_options;
};

} // namespace pipy

#endif // MUX_HPP
