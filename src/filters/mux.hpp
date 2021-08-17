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
#include "session.hpp"
#include "list.hpp"
#include "timer.hpp"

#include <queue>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace pipy {

class Data;
class Pipeline;

//
// MuxBase
//

class MuxBase : public Filter {
public:

  //
  // MuxBase::Connection
  //

  class Connection {
  public:
    class Stream {
    public:
      virtual void input(Data *data) = 0;
      virtual void end() = 0;
      virtual void close() = 0;
    };

    virtual auto stream(MessageStart *start, const Event::Receiver &on_output) -> Stream* = 0;
    virtual void receive(Event *evt) = 0;
    virtual void close() = 0;

  protected:
    ~Connection() {
      if (m_session) {
        m_session->on_output(nullptr);
      }
    }

    void send(Event *evt);
    void reset();

  private:
    Pipeline* m_pipeline = nullptr;
    pjs::Ref<Context> m_context;
    pjs::Value m_key;
    pjs::Ref<Session> m_session;
    int m_share_count = 1;
    double m_free_time = 0;

    friend class MuxBase;
  };

protected:
  MuxBase();
  MuxBase(const MuxBase &r);
  MuxBase(pjs::Str *target, const pjs::Value &channel);

  virtual auto new_connection() -> Connection* = 0;

private:
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  //
  // MuxBase::ConnectionManager
  //

  class ConnectionManager {
  public:
    ConnectionManager(MuxBase *mux)
      : m_mux(mux) { recycle(); }

    auto get(const pjs::Value &key) -> Connection*;
    void free(Connection *connection);

  private:
    MuxBase* m_mux;
    std::unordered_map<pjs::Value, Connection*> m_connections;
    std::unordered_set<Connection*> m_free_connections;
    Timer m_recycle_timer;

    void recycle();
  };

  std::shared_ptr<ConnectionManager> m_connection_manager;
  pjs::Ref<pjs::Str> m_target;
  pjs::Value m_channel;
  Connection* m_connection = nullptr;
  Connection::Stream* m_stream = nullptr;
  bool m_session_end = false;
};

//
// Mux
//

class Mux : public Filter {
public:
  Mux();
  Mux(pjs::Str *target, pjs::Function *selector);

private:
  Mux(const Mux &r);
  ~Mux();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

private:
  struct Channel : public pjs::Pooled<Channel> {
    Event::Receiver on_output;
  };

  class SessionPool;

  class SharedSession :
    public List<SharedSession>::Item,
    public pjs::RefCount<SharedSession>
  {
  public:
    SharedSession(Pipeline *pipeline, pjs::Str *name, int buffer_limit)
      : m_pipeline(pipeline)
      , m_name(name)
      , m_buffer_limit(buffer_limit) {}

    void input(Channel *channel, Context *ctx, pjs::Object *mctx, pjs::Object *head, Data *body);

  private:
    int m_share_count = 1;
    int m_free_time;
    int m_buffer_limit;
    Pipeline* m_pipeline;
    pjs::Ref<pjs::Str> m_name;
    pjs::Ref<Session> m_session;
    std::queue<std::unique_ptr<Channel>> m_queue;

    friend class SessionPool;
  };

  class SessionPool {
  public:
    auto alloc(Pipeline *pipeline, pjs::Str *name) -> SharedSession* {
      auto i = m_sessions.find(name);
      if (i != m_sessions.end()) {
        auto session = i->second.get();
        if (!session->m_share_count++) m_free_sessions.remove(session);
        return session;
      }
      auto session = new SharedSession(pipeline, name, m_buffer_limit);
      m_sessions[name] = session;
      return session;
    }

    void free(SharedSession *session) {
      if (session) {
        if (!--session->m_share_count) {
          session->m_free_time = 0;
          m_free_sessions.push(session);
          start_cleaning();
        }
      }
    }

  private:
    int m_buffer_limit = 100;
    std::unordered_map<pjs::Ref<pjs::Str>, pjs::Ref<SharedSession>> m_sessions;
    List<SharedSession> m_free_sessions;
    bool m_cleaning_scheduled = false;
    Timer m_timer;

    void start_cleaning();
    void clean();
  };

  std::shared_ptr<SessionPool> m_session_pool;
  pjs::Ref<pjs::Str> m_target;
  pjs::Ref<pjs::Function> m_selector;
  pjs::Ref<pjs::Object> m_mctx;
  pjs::Ref<pjs::Object> m_head;
  pjs::Ref<Data> m_body;
  pjs::Ref<SharedSession> m_session;
  std::queue<Channel*> m_queue;
  bool m_session_end = false;
};

} // namespace pipy

#endif // MUX_HPP