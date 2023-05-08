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
#include "message.hpp"
#include "input.hpp"
#include "list.hpp"
#include "timer.hpp"
#include "options.hpp"

#include <unordered_map>

namespace pipy {

class MuxSession;
class MuxSessionPool;
class MuxSessionMap;
class MuxSource;

//
// MuxSession
//

class MuxSession : public AutoReleased, public List<MuxSession>::Item {
public:

  //
  // MuxSession::Options
  //

  struct Options : public pipy::Options {
    double max_idle = 60;
    int max_queue = 0;
    int max_messages = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  //
  // MuxSession::StartInfo
  //

  struct StartInfo : public pjs::ObjectTemplate<StartInfo> {
    pjs::Value sessionKey;
    int sessionCount = 0;
  };

  virtual void mux_session_open(MuxSource *source) = 0;
  virtual auto mux_session_open_stream(MuxSource *source) -> EventFunction* = 0;
  virtual void mux_session_close_stream(EventFunction *stream) = 0;
  virtual void mux_session_close() = 0;

protected:
  auto pool() const -> MuxSessionPool* { return m_pool; }
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  bool is_open() const { return m_pipeline; }
  bool is_free() const { return !m_share_count; }
  bool is_pending() const { return m_is_pending; }
  void set_pending(bool pending);
  void detach();
  void end(StreamEnd *eos);

private:
  void open(MuxSource *source, Pipeline *pipeline);
  void close();
  void free();

  MuxSessionPool* m_pool = nullptr;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<StreamEnd> m_eos;
  List<MuxSource> m_waiting_sources;
  int m_share_count = 1;
  int m_message_count = 0;
  double m_free_time = 0;
  bool m_is_pending = false;

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

  pjs::Ref<MuxSessionMap> m_map;
  pjs::Value m_key;
  pjs::WeakRef<pjs::Object> m_weak_key;
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
  std::unordered_map<pjs::WeakRef<pjs::Object>, MuxSessionPool*> m_weak_pools;
  List<MuxSessionPool> m_recycle_pools;
  Timer m_recycle_timer;
  bool m_has_recycling_scheduled = false;
  bool m_has_shutdown = false;

  auto alloc(const pjs::Value &key, MuxSource *source) -> MuxSession*;
  void schedule_recycling();

  friend class pjs::RefCount<MuxSessionMap>;
  friend class MuxSource;
  friend class MuxSessionPool;
};

//
// MuxSource
//

class MuxSource : public List<MuxSource>::Item, public EventProxy {
protected:
  MuxSource();
  MuxSource(const MuxSource &r);

  void reset();
  void key(const pjs::Value &key) { m_session_key = key; }
  auto map() -> MuxSessionMap* { return m_map; }

  virtual auto on_mux_new_pool() -> MuxSessionPool* = 0;
  virtual auto on_mux_new_pipeline() -> Pipeline* = 0;

private:
  virtual void on_input(Event *evt) override;
  virtual void on_reply(Event *evt) override;

  pjs::Ref<MuxSessionMap> m_map;
  pjs::Ref<MuxSession> m_session;
  pjs::Value m_session_key;
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
  void reset();
  auto stream(MuxSource *source) -> EventFunction*;
  void close(EventFunction *stream);
  void increase_output_count(int n);
  void dedicate();

  virtual auto on_queue_message(MuxSource *source, Message *msg) -> int { return 1; }
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
  protected:
    Stream(MuxQueue *queue, MuxSource *source)
      : m_queue(queue)
      , m_source(source) {}

    virtual void on_event(Event *evt) override;

  private:
    MuxQueue* m_queue;
    MuxSource* m_source;
    MessageReader m_reader;
    pjs::Ref<StreamEnd> m_eos;
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
  };

  List<Receiver> m_receivers;
  pjs::Ref<Stream> m_dedicated_stream;

  friend class Stream;
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
    virtual void mux_session_open(MuxSource *source) override;
    virtual auto mux_session_open_stream(MuxSource *source) -> EventFunction* override;
    virtual void mux_session_close_stream(EventFunction *stream) override;
    virtual void mux_session_close() override;
    virtual auto on_queue_message(MuxSource *source, Message *msg) -> int override;
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

} // namespace pipy

#endif // MUX_HPP
