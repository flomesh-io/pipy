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

#ifndef FCGI_HPP
#define FCGI_HPP

#include "mux.hpp"
#include "demux.hpp"
#include "data.hpp"

namespace pipy {
namespace fcgi {

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  int version = 1;
  int requestID = 0;
};

class RequestHead : public pjs::ObjectTemplate<RequestHead, MessageHead> {
public:
  int role = 1;
  int flags = 0;
  pjs::Ref<pjs::Array> params;
};

class ResponseHead : public pjs::ObjectTemplate<ResponseHead, MessageHead> {
public:
  int appStatus = 0;
  int protocolStatus = 0;
};

class ResponseTail : public pjs::ObjectTemplate<ResponseTail> {
public:
  pjs::Ref<Data> stderr_data;
};

//
// Demux
//

class Demux : public Filter, protected DemuxSession {
public:
  Demux();

protected:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void shutdown() override;
  virtual void dump(Dump &d) override;

  pjs::Ref<StreamEnd> m_eos;

  virtual auto on_demux_open_stream() -> EventFunction* override;
  virtual void on_demux_close_stream(EventFunction *stream) override;
  virtual void on_demux_complete() override;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  Mux();
  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const MuxSession::Options &options);
  Mux(pjs::Function *session_selector, pjs::Function *options);

private:
  Mux(const Mux &mux);

  MuxSession::Options m_options;

  virtual void dump(Dump &d) override;
  virtual auto clone() -> Filter* override;
  virtual auto on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* override;

  //
  // Mux::Session
  //

  class Session : public pjs::Pooled<Session, MuxSession> {
    virtual void mux_session_open(MuxSource *source) override;
    virtual auto mux_session_open_stream(MuxSource *source) -> EventFunction* override;
    virtual void mux_session_close_stream(EventFunction *stream) override;
    virtual void mux_session_close() override;
    virtual void on_auto_release() override { delete this; }
  };

  //
  // Mux::SessionPool
  //

  struct SessionPool : public pjs::Pooled<SessionPool, MuxSessionPool> {
    SessionPool(const MuxSession::Options &options)
      : pjs::Pooled<SessionPool, MuxSessionPool>(options) {}

    virtual auto session() -> MuxSession* override { return new Session(); }
    virtual void free() override { delete this; }
  };
};

} // namespace fcgi
} // namespace pipy

#endif // FCGI_HPP
