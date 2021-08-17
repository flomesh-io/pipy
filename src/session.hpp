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

#ifndef SESSION_HPP
#define SESSION_HPP

#include "context.hpp"
#include "pipeline.hpp"
#include "event.hpp"
#include "filter.hpp"
#include "message.hpp"
#include "inbound.hpp"

#include <list>

namespace pipy {

class Session;
class Filter;

//
// ReusableSession
//

class ReusableSession {
public:
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  auto session() const -> Session* { return m_session; }

private:
  ReusableSession(Pipeline *pipeline);
  ~ReusableSession();

  void input(Event *evt);
  void input(Message *msg);
  void abort();
  void free();

  void enter_processing() {
    m_processing_level++;
  }

  void leave_processing() {
    if (--m_processing_level == 0) {
      if (m_freed) {
        free();
      }
    }
  }

  Pipeline* m_pipeline;
  Filter* m_filters;
  ReusableSession* m_next = nullptr;
  Session* m_session = nullptr;
  pjs::Ref<Context> m_context;
  Event::Receiver m_output;
  int m_processing_level = 0;
  bool m_done = false;
  bool m_freed = false;

  void reset();

  friend class Pipeline;
  friend class Session;
  friend class Filter;
};

//
// Session
//

class Session : public pjs::ObjectTemplate<Session> {
public:
  auto context() const -> Context* {
    return m_reusable_session->m_context;
  }

  auto pipeline() const -> Pipeline* {
    return m_reusable_session->m_pipeline;
  }

  auto remote_address() const -> pjs::Str* {
    auto inbound = m_reusable_session->m_context->inbound();
    return inbound ? inbound->remote_address() : pjs::Str::empty.get();
  }

  auto local_address() const -> pjs::Str* {
    auto inbound = m_reusable_session->m_context->inbound();
    return inbound ? inbound->local_address() : pjs::Str::empty.get();
  }

  auto remote_port() const -> int {
    auto inbound = m_reusable_session->m_context->inbound();
    return inbound ? inbound->remote_port() : 0;
  }

  auto local_port() const -> int {
    auto inbound = m_reusable_session->m_context->inbound();
    return inbound ? inbound->local_port() : 0;
  }

  void input(Event *evt) {
    m_reusable_session->input(evt);
  }

  void input(Message *msg) {
    m_reusable_session->input(msg);
  }

  void on_output(Event::Receiver out) { m_reusable_session->m_output = out; }

  bool done() const { return m_reusable_session->m_done; }

private:
  Session(Context *ctx, Pipeline *pipeline)
    : m_reusable_session(pipeline->alloc())
  {
    m_reusable_session->m_session = this;
    m_reusable_session->m_context = ctx;
  }

  ~Session() {
    m_reusable_session->m_session = nullptr;
    m_reusable_session->free();
  }

  ReusableSession* m_reusable_session;

  friend class pjs::ObjectTemplate<Session>;
};

} // namespace pipy

#endif // SESSION_HPP