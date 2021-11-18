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
#include "list.hpp"
#include "timer.hpp"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace pipy {

class Data;
class PipelineDef;

//
// MuxBase
//

class MuxBase : public Filter {
protected:

  //
  // MuxBase::Demux
  //

  struct Demux : public EventFunction {
    virtual void on_event(Event *evt) override;
  };

  //
  // MuxBase::Session
  //

  class Session : protected Demux {
  public:

    //
    // MuxBase::Session::Stream
    //

    class Stream : public EventFunction {
    public:
      virtual void close() = 0;
    };

    virtual void open(Pipeline *pipeline);
    virtual auto stream() -> Stream* = 0;
    virtual void input(Event *evt);
    virtual void on_demux(Event *evt) = 0;
    virtual void close();

  private:
    pjs::Ref<Pipeline> m_pipeline;
    int m_share_count = 1;
    double m_free_time = 0;

    friend class MuxBase;
  };

protected:
  MuxBase();
  MuxBase(const pjs::Value &key);
  MuxBase(const MuxBase &r);

  virtual void reset() override;
  virtual void process(Event *evt) override;

  virtual auto on_new_session() -> Session* = 0;

private:

  //
  // MuxBase::SessionManager
  //

  class SessionManager : public pjs::RefCount<SessionManager> {
  public:
    SessionManager(MuxBase *mux)
      : m_mux(mux) { recycle(); }

    auto get(const pjs::Value &key) -> Session*;
    void free(Session *session);

  private:
    MuxBase* m_mux;
    std::unordered_map<pjs::Value, Session*> m_sessions;
    std::unordered_set<Session*> m_free_sessions;
    Timer m_recycle_timer;
    bool m_retained_for_free_sessions = false;

    void retain_for_free_sessions();
    void recycle();
  };

  pjs::Ref<SessionManager> m_session_manager;
  pjs::Value m_target_key;
  Session* m_session = nullptr;
  Session::Stream* m_stream = nullptr;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  Mux();
  Mux(const pjs::Value &key);

protected:
  Mux(const Mux &r);
  ~Mux();

  virtual auto clone() -> Filter* override;
  virtual void dump(std::ostream &out) override;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session>,
    public MuxBase::Session
  {
  public:

    //
    // Mux::Session::Stream
    //

    class Stream :
      public pjs::Pooled<Stream>,
      public List<Stream>::Item,
      public MuxBase::Session::Stream
    {
    protected:
      Stream(Session *session)
        : m_session(session) {}

      virtual void on_event(Event *evt) override;
      virtual void close() override;

      auto session() const -> Session* { return m_session; }

    private:
      Session* m_session;
      pjs::Ref<MessageStart> m_start;
      Data m_buffer;
      bool m_started = false;
      bool m_queued = false;
      bool m_ended = false;

      friend class Session;
    };

  protected:
    virtual auto stream() -> Stream* override;
    virtual void on_demux(Event *evt) override;
    virtual void close() override;

  private:
    List<Stream> m_streams;

    void close(StreamEnd *end);

    friend class Stream;
  };

  virtual auto on_new_session() -> Session* override {
    return new Session();
  }
};

} // namespace pipy

#endif // MUX_HPP
