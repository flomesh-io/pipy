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
    Options() {}
    Options(pjs::Object *options);
  };

protected:
  class Session;

  MuxBase();
  MuxBase(pjs::Function *group);
  MuxBase(pjs::Function *group, const Options &options);
  MuxBase(const MuxBase &r);

  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual auto on_new_session() -> Session* = 0;

private:
  class SessionCluster;
  class SessionManager;

  pjs::Ref<SessionManager> m_session_manager;
  pjs::Ref<Session> m_session;
  pjs::Ref<pjs::Function> m_group;
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
    bool isolated() const { return !m_cluster; }
    void isolate();
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
    double m_free_time = 0;
    List<MuxBase> m_waiting_muxers;
    bool m_is_pending = false;

    virtual void on_input(Event *evt) override;
    virtual void on_reply(Event *evt) override;
    virtual void on_recycle() override { delete this; }

    friend class pjs::RefCount<Session>;
    friend class MuxBase;
  };

private:

  //
  // MuxBase::SessionCluter
  //

  class SessionCluster :
    public pjs::Pooled<SessionCluster>,
    public pjs::Object::WeakPtr::Watcher,
    public List<SessionCluster>::Item
  {
    SessionCluster(SessionManager *manager)
      : m_manager(manager) {}

    void reset();
    auto alloc(int max_share_count) -> Session*;
    void free(Session *session);
    void discard(Session *session);

    pjs::Ref<SessionManager> m_manager;
    pjs::Value m_key;
    pjs::WeakRef<pjs::Object> m_weak_key;
    List<Session> m_sessions;
    bool m_recycle_scheduled = false;

    void sort(Session *session);
    void recycle(double now, double max_idle);

    virtual void on_weak_ptr_gone() override { reset(); }

    friend class MuxBase;
  };

  //
  // MuxBase::SessionManager
  //

  class SessionManager : public pjs::RefCount<SessionManager> {
    SessionManager(MuxBase *mux)
      : m_mux(mux) {}

    ~SessionManager();

    auto get(const pjs::Value &key) -> Session*;
    void shutdown();

    MuxBase* m_mux;
    std::unordered_map<pjs::Value, SessionCluster*> m_clusters;
    std::unordered_map<pjs::WeakRef<pjs::Object>, SessionCluster*> m_weak_clusters;
    List<SessionCluster> m_recycle_clusters;
    double m_max_idle = 10;
    int m_max_queue = 0;
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
  void increase_queue_count();
  void reset();
  void isolate();

private:
  class Stream;

  pjs::Ref<Input> m_session_input;
  List<Stream> m_streams;
  bool m_isolated = false;

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
    bool m_started = false;
    bool m_isolated = false;

    friend class QueueMuxer;
  };

  friend class Stream;
};

//
// MuxQueue
//

class MuxQueue : public MuxBase {
public:
  MuxQueue();
  MuxQueue(pjs::Function *group);
  MuxQueue(pjs::Function *group, const Options &options);

protected:
  MuxQueue(const MuxQueue &r);
  ~MuxQueue();

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual auto on_new_session() -> MuxBase::Session* override;

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
};

//
// Mux
//

class Mux : public MuxBase {
public:
  Mux();
  Mux(pjs::Function *group);
  Mux(pjs::Function *group, const Options &options);

private:
  Mux(const Mux &r);
  ~Mux();

  virtual auto clone() -> Filter* override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
  virtual auto on_new_session() -> MuxBase::Session* override;

  //
  // Mux::Session
  //

  class Session : public pjs::Pooled<Session, MuxBase::Session> {
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;

    friend class Stream;
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
