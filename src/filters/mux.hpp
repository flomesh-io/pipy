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
#include "list.hpp"
#include "timer.hpp"

#include <unordered_map>

namespace pipy {

class Data;
class PipelineDef;

//
// MuxBase
//

class MuxBase : public Filter {
protected:
  class Session;

  MuxBase();
  MuxBase(const pjs::Value &session_key, pjs::Object *options);
  MuxBase(const MuxBase &r);

  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual auto on_new_session() -> Session* = 0;

private:
  class SessionManager;

  pjs::Ref<SessionManager> m_session_manager;
  pjs::Ref<Session> m_session;
  pjs::Value m_session_key;
  EventFunction* m_stream = nullptr;

protected:

  //
  // MuxBase::Session
  //

  class Session :
    public pjs::Pooled<Session>,
    public pjs::RefCount<Session>,
    public List<Session>::Item,
    public EventProxy
  {
  protected:
    virtual void open();
    virtual auto open_stream() -> EventFunction* = 0;
    virtual void close_stream(EventFunction *stream) = 0;
    virtual void close();

    Session() {}
    virtual ~Session() {}

    bool isolated() const { return !m_manager; }
    void isolate();

  private:
    void start(Pipeline *pipeline);
    bool started() const { return m_pipeline; }
    void end();
    void free();

    SessionManager* m_manager = nullptr;
    pjs::Value m_key;
    pjs::WeakRef<pjs::Object> m_weak_key;
    pjs::Ref<Pipeline> m_pipeline;
    int m_share_count = 1;
    double m_free_time = 0;

    virtual void on_input(Event *evt) override;
    virtual void on_reply(Event *evt) override;

    friend class pjs::RefCount<Session>;
    friend class MuxBase;
  };

private:

  //
  // MuxBase::SessionManager
  //

  class SessionManager : public pjs::RefCount<SessionManager> {
    struct Options {
      int max_idle = 10;
    };

    SessionManager(MuxBase *mux)
      : m_mux(mux) {}

    void set_options(const Options &options) { m_options = options; }
    auto get(const pjs::Value &key) -> Session*;
    void free(Session *session);

    MuxBase* m_mux;
    std::unordered_map<pjs::Value, pjs::Ref<Session>> m_sessions;
    std::unordered_map<pjs::WeakRef<pjs::Object>, pjs::Ref<Session>> m_weak_sessions;
    List<Session> m_free_sessions;
    Options m_options;
    Timer m_recycle_timer;
    bool m_recycling = false;

    void erase(Session *session);
    void recycle();

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
  void reset();
  void isolate();

private:
  class Stream;

  pjs::Ref<Input> m_session_input;
  List<Stream> m_streams;
  bool m_isolated = false;

  void on_event(Event *evt) override;

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
    bool m_started = false;
    bool m_queued = false;
    bool m_ended = false;
    bool m_isolated = false;

    friend class QueueMuxer;
  };

  friend class Stream;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  Mux();
  Mux(const pjs::Value &key, pjs::Object *options);

protected:
  Mux(const Mux &r);
  ~Mux();

  virtual auto clone() -> Filter* override;
  virtual void dump(std::ostream &out) override;
  virtual auto on_new_session() -> MuxBase::Session* override;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxBase::Session>,
    public QueueMuxer
  {
    virtual void open() override;
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;
    virtual void close() override;

    friend class Mux;
  };
};

} // namespace pipy

#endif // MUX_HPP
