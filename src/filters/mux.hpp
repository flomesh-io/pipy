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
#include "input.hpp"
#include "context.hpp"
#include "list.hpp"
#include "timer.hpp"
#include "options.hpp"

#include <unordered_map>

namespace pipy {

class Data;

//
// MuxBase
//

class MuxBase :
  public Filter,
  public List<MuxBase>::Item
{
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

  MuxBase();
  MuxBase(pjs::Function *session_selector);
  MuxBase(pjs::Function *session_selector, const Options &options);
  MuxBase(pjs::Function *session_selector, pjs::Function *options);
  MuxBase(const MuxBase &r);

  auto session() -> Session* { return m_session; }
  auto stream() -> EventFunction* { return m_stream; }

  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual auto on_new_cluster(pjs::Object *options) -> SessionCluster* = 0;

private:
  class SessionManager;

  Options m_options;
  pjs::Ref<pjs::Function> m_options_f;
  pjs::Ref<SessionManager> m_session_manager;
  pjs::Ref<Session> m_session;
  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Value m_session_key;
  EventFunction* m_stream = nullptr;
  EventBuffer m_waiting_events;
  bool m_waiting = false;

  void open_stream();
  void start_waiting();
  void flush_waiting();
  void stop_waiting();

protected:

  //
  // MuxBase::Session
  //

  class Session :
    public pjs::Pooled<Session>,
    public List<Session>::Item,
    public AutoReleased,
    public EventProxy
  {
  protected:
    virtual void open();
    virtual auto open_stream() -> EventFunction* = 0;
    virtual void close_stream(EventFunction *stream) = 0;
    virtual void close();

    Session() {}
    virtual ~Session() {}

    auto pipeline() const -> Pipeline* { return m_pipeline; }
    bool dedicated() const { return !m_cluster; }
    void dedicate();
    bool is_free() const { return !m_share_count; }
    bool is_pending() const { return m_is_pending; }
    void set_pending(bool pending);

  private:
    void init(Pipeline *pipeline);
    void free();
    void reset();

    SessionCluster* m_cluster = nullptr;
    pjs::Ref<Pipeline> m_pipeline;
    int m_share_count = 1;
    int m_message_count = 0;
    double m_free_time = 0;
    List<MuxBase> m_waiting_muxers;
    bool m_is_pending = false;
    bool m_is_closed = false;

    virtual void on_input(Event *evt) override;
    virtual void on_reply(Event *evt) override;
    virtual void on_recycle() override { delete this; }

    friend class pjs::RefCount<Session>;
    friend class MuxBase;
  };

  //
  // MuxBase::SessionCluter
  //

  class SessionCluster :
    public pjs::Pooled<SessionCluster>,
    public pjs::Object::WeakPtr::Watcher,
    public List<SessionCluster>::Item
  {
  protected:
    SessionCluster(MuxBase *mux, pjs::Object *options);
    virtual auto session() -> Session* = 0;
    virtual void free() = 0;

  private:
    auto alloc() -> Session*;
    void free(Session *session);
    void discard(Session *session);

    pjs::Ref<SessionManager> m_manager;
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

    friend class MuxBase;
  };

private:

  //
  // MuxBase::SessionManager
  //

  class SessionManager : public pjs::RefCount<SessionManager> {
    ~SessionManager();

    auto get(MuxBase *mux, const pjs::Value &key) -> Session*;
    void shutdown();

    std::unordered_map<pjs::Value, SessionCluster*> m_clusters;
    std::unordered_map<pjs::WeakRef<pjs::Object>, SessionCluster*> m_weak_clusters;
    List<SessionCluster> m_recycle_clusters;
    Timer m_recycle_timer;
    bool m_recycling = false;
    bool m_has_shutdown = false;

    void recycle();

    friend class pjs::RefCount<SessionManager>;
    friend class MuxBase;
  };
};

//
// QueueMuxer
//

class QueueMuxer : public EventSource {
public:
  auto open() -> EventFunction*;
  void close(EventFunction *stream);
  void set_one_way(EventFunction *stream);
  void increase_queue_count();
  void reset();
  void dedicate();

private:
  class Stream;

  pjs::Ref<Input> m_session_input;
  List<Stream> m_streams;
  bool m_dedicated = false;

  void on_reply(Event *evt) override;

  //
  // QueueMuxer::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public pjs::RefCount<Stream>,
    public List<Stream>::Item,
    public EventFunction
  {
  protected:
    Stream(QueueMuxer *muxer)
      : m_muxer(muxer) {}

    virtual void on_event(Event *evt) override;

  private:
    QueueMuxer* m_muxer;
    pjs::Ref<MessageStart> m_start;
    Data m_buffer;
    int m_queued_count = 0;
    bool m_one_way = false;
    bool m_started = false;
    bool m_dedicated = false;

    friend class QueueMuxer;
  };

  friend class Stream;
};

//
// MuxQueue
//

class MuxQueue : public MuxBase {
public:
  struct Options : public MuxBase::Options {
    pjs::Ref<pjs::Function> is_one_way;
    Options() {}
    Options(pjs::Object *options);
  };

  MuxQueue();
  MuxQueue(pjs::Function *session_selector);
  MuxQueue(pjs::Function *session_selector, const Options &options);
  MuxQueue(pjs::Function *session_selector, pjs::Function *options);

protected:
  MuxQueue(const MuxQueue &r);
  ~MuxQueue();

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual auto on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* override;

  //
  // MuxQueue::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxBase::Session>,
    public QueueMuxer
  {
    virtual void open() override;
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;
    virtual void close() override;

    friend class MuxQueue;
  };

  //
  // MuxQueue::SessionCluster
  //

  class SessionCluster : public pjs::Pooled<SessionCluster, MuxBase::SessionCluster> {
    using pjs::Pooled<SessionCluster, MuxBase::SessionCluster>::Pooled;

    virtual auto session() -> Session* override { return new Session(); }
    virtual void free() override { delete this; }

    friend class MuxQueue;
  };

private:
  Options m_options;
  bool m_started = false;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  Mux();
  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const Options &options);
  Mux(pjs::Function *session_selector, pjs::Function *options);

private:
  Mux(const Mux &r);
  ~Mux();

  virtual auto clone() -> Filter* override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
  virtual auto on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* override;

  //
  // Mux::Session
  //

  class Session : public pjs::Pooled<Session, MuxBase::Session> {
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;

    friend class Stream;
  };

  //
  // Mux::SessionCluster
  //

  class SessionCluster : public pjs::Pooled<SessionCluster, MuxBase::SessionCluster> {
    using pjs::Pooled<SessionCluster, MuxBase::SessionCluster>::Pooled;

    virtual auto session() -> MuxBase::Session* override { return new Session(); }
    virtual void free() override { delete this; }

    friend class Mux;
  };

  //
  // Mux::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public EventFunction
  {
    Stream(Session *session)
      : m_output(session->input()) {}

    virtual void on_event(Event *evt) override;

    pjs::Ref<Input> m_output;
    pjs::Ref<MessageStart> m_start;
    Data m_buffer;

    friend class Session;
  };
};

} // namespace pipy

#endif // MUX_HPP
