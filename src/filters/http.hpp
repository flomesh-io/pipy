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

#ifndef HTTP_HPP
#define HTTP_HPP

#include "mux.hpp"
#include "demux.hpp"
#include "data.hpp"
#include "list.hpp"
#include "api/http.hpp"
#include "http2.hpp"
#include "options.hpp"

namespace pipy {
namespace http {

//
// RequestQueue
//

class RequestQueue {
public:

  //
  // RequestQueue::Request
  //

  struct Request :
    public pjs::Pooled<Request>,
    public List<Request>::Item
  {
    pjs::Ref<RequestHead> head;
    bool is_final = false;
    TunnelType tunnel_type = TunnelType::NONE;
  };

  bool empty() const { return m_queue.empty(); }
  void reset() { while (auto *r = m_queue.head()) { m_queue.remove(r); delete r; } }
  void push(Request *req) { m_queue.push(req); }
  auto head() const -> Request* { return m_queue.head(); }
  auto shift() -> Request* { auto r = head(); if (r) m_queue.remove(r); return r; }

private:
  List<Request> m_queue;
};

//
// Decoder
//

class Decoder : public EventFunction {
public:
  Decoder(bool is_response)
    : m_is_response(is_response) {}

  void reset();
  bool has_error() const { return m_has_error; }
  void set_max_header_size(size_t size) { m_max_header_size = size; }
  void set_tunnel() { m_is_tunnel = true; }

protected:
  virtual void on_decode_request(RequestQueue::Request *req) { delete req; }
  virtual auto on_decode_response(ResponseHead *head) -> RequestQueue::Request* { return nullptr; }
  virtual bool on_decode_tunnel(TunnelType tt) { return false; }
  virtual void on_decode_error() {}

private:
  enum State {
    HEAD,
    HEAD_EOL,
    HEADER,
    HEADER_EOL,
    BODY,
    CHUNK_HEAD,
    CHUNK_BODY,
    CHUNK_TAIL,
    CHUNK_LAST,
    HTTP2_PREFACE,
    HTTP2_PASS,
  };

  State m_state = HEAD;
  Data m_head_buffer;
  size_t m_max_header_size = DATA_CHUNK_SIZE;
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Str> m_method;
  pjs::Ref<pjs::Str> m_header_transfer_encoding;
  pjs::Ref<pjs::Str> m_header_content_length;
  pjs::Ref<pjs::Str> m_header_connection;
  pjs::Ref<pjs::Str> m_header_upgrade;
  TunnelType m_responded_tunnel_type = TunnelType::NONE;
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_tunnel = false;
  bool m_has_error = false;

  virtual void on_event(Event *evt) override;

  void message_start();
  void message_end();
  void stream_end(StreamEnd *eos);

  void error() {
    m_has_error = true;
    on_decode_error();
  }
};

//
// Encoder
//

class Encoder : public EventFunction {
public:
  Encoder(bool is_response);

  void reset();
  void set_buffer_size(int size) { m_buffer_size = size; }
  void set_tunnel() { m_is_tunnel = true; }

protected:
  virtual void on_encode_request(RequestQueue::Request *req) { delete req; }
  virtual auto on_encode_response(ResponseHead *head) -> RequestQueue::Request* { return nullptr; }
  virtual bool on_encode_tunnel(TunnelType tt) { return false; }

private:
  Data m_buffer;
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Str> m_protocol;
  pjs::Ref<pjs::Str> m_method;
  pjs::Ref<pjs::Str> m_path;
  pjs::Ref<pjs::Str> m_header_connection;
  pjs::Ref<pjs::Str> m_header_upgrade;
  TunnelType m_responded_tunnel_type = TunnelType::NONE;
  int m_buffer_size = DATA_CHUNK_SIZE;
  int m_status_code = 0;
  int m_content_length = 0;
  bool m_chunked = false;
  bool m_is_response;
  bool m_is_final = false;
  bool m_is_tunnel = false;

  virtual void on_event(Event *evt) override;

  void output_head();
  void output_chunk(const Data &data);
  void output_end(Event *evt);
};

//
// RequestDecoder
//

class RequestDecoder : public Filter, public Decoder {
public:
  RequestDecoder(pjs::Function *handler = nullptr);

private:
  RequestDecoder(const RequestDecoder &r);
  ~RequestDecoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<pjs::Function> m_handler;

  virtual void on_decode_request(RequestQueue::Request *req) override;
};

//
// ResponseDecoder
//

class ResponseDecoder : public Filter, public Decoder {
public:
  ResponseDecoder(pjs::Function *handler = nullptr);

private:
  ResponseDecoder(const ResponseDecoder &r);
  ~ResponseDecoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<pjs::Function> m_handler;

  virtual auto on_decode_response(ResponseHead *head) -> RequestQueue::Request* override;
};

//
// RequestEncoder
//

class RequestEncoder : public Filter, public Encoder {
public:
  struct Options : pipy::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    Options() {}
    Options(pjs::Object *options);
  };

  RequestEncoder(const Options &options, pjs::Function *handler = nullptr);

private:
  RequestEncoder(const RequestEncoder &r);
  ~RequestEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
  pjs::Ref<pjs::Function> m_handler;

  virtual void on_encode_request(RequestQueue::Request *req) override;
};

//
// ResponseEncoder
//

class ResponseEncoder : public Filter, public Encoder {
public:
  struct Options : pipy::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    Options() {}
    Options(pjs::Object *options);
  };

  ResponseEncoder(const Options &options, pjs::Function *handler = nullptr);

private:
  ResponseEncoder(const ResponseEncoder &r);
  ~ResponseEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
  pjs::Ref<pjs::Function> m_handler;

  virtual auto on_encode_response(ResponseHead *head) -> RequestQueue::Request* override;
};

//
// Demux
//

class Demux :
  public Filter,
  protected DemuxQueue,
  protected Decoder,
  protected Encoder,
  protected http2::Server
{
public:
  struct Options : public http2::Endpoint::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    size_t max_header_size = DATA_CHUNK_SIZE;
    Options() {}
    Options(pjs::Object *options);
  };

  Demux(const Options &options);

protected:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void shutdown() override;
  virtual void dump(Dump &d) override;

  Options m_options;
  RequestQueue m_request_queue;
  pjs::Ref<StreamEnd> m_eos;
  bool m_http2 = false;
  bool m_shutdown = false;

  virtual auto on_demux_open_stream() -> EventFunction* override;
  virtual void on_demux_close_stream(EventFunction *stream) override;
  virtual void on_demux_complete() override;

  virtual void on_decode_error() override;
  virtual void on_decode_request(RequestQueue::Request *req) override;
  virtual auto on_encode_response(ResponseHead *head) -> RequestQueue::Request* override;
  virtual bool on_decode_tunnel(TunnelType tt) override;
  virtual bool on_encode_tunnel(TunnelType tt) override;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  struct Options :
    public MuxSession::Options,
    public http2::Endpoint::Options
  {
    size_t buffer_size = DATA_CHUNK_SIZE;
    size_t max_header_size = DATA_CHUNK_SIZE;
    int version = 1;
    pjs::Ref<pjs::Str> version_s;
    pjs::Ref<pjs::Function> version_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Mux();
  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const Options &options);
  Mux(pjs::Function *session_selector, pjs::Function *options);

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxSession>,
    protected MuxQueue,
    protected Encoder,
    protected Decoder,
    protected http2::Client
  {
  public:
    Session(const Mux::Options &options);
    ~Session();

    //
    // Mux::Session::VersionSelector
    //

    class VersionSelector : public pjs::ObjectTemplate<VersionSelector> {
    public:
      void select(const pjs::Value &version) {
        if (m_session) m_session->select_protocol(m_mux, version);
      }
      void close() { m_session = nullptr; }
    private:
      VersionSelector(Mux *mux, Session *session)
        : m_mux(mux), m_session(session) {}
      Mux* m_mux;
      Session* m_session;
      friend class pjs::ObjectTemplate<VersionSelector>;
    };

  private:
    virtual void mux_session_open(MuxSource *source) override;
    virtual auto mux_session_open_stream(MuxSource *source) -> EventFunction* override;
    virtual void mux_session_close_stream(EventFunction *stream) override;
    virtual void mux_session_close() override;

    virtual void on_encode_request(RequestQueue::Request *req) override;
    virtual auto on_decode_response(ResponseHead *head) -> RequestQueue::Request* override;
    virtual bool on_decode_tunnel(TunnelType tt) override;
    virtual void on_decode_error() override;
    virtual void on_queue_end(StreamEnd *eos) override;
    virtual void on_endpoint_close(StreamEnd *eos) override;
    virtual void on_auto_release() override { delete this; }

    const Mux::Options& m_options;
    int m_version_selected = 0;
    pjs::Ref<VersionSelector> m_version_selector;
    RequestQueue m_request_queue;
    bool m_http2 = false;

    bool select_protocol(Mux *muxer);
    bool select_protocol(Mux *muxer, const pjs::Value &version);
  };

private:
  Mux(const Mux &r);
  ~Mux();

  Options m_options;
  EventBuffer m_waiting_events;

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual auto on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* override;

  auto verify_http_version(int version) -> int;
  auto verify_http_version(pjs::Str *name) -> int;

  //
  // Mux::SessionPool
  //

  struct SessionPool : public pjs::Pooled<SessionPool, MuxSessionPool> {
    SessionPool(const Options &options)
      : pjs::Pooled<SessionPool, MuxSessionPool>(options)
      , m_options(options) {}

    virtual auto session() -> MuxSession* override { return new Session(m_options); }
    virtual void free() override { delete this; }

    Options m_options;
  };
};

//
// Server
//

class Server : public Demux {
public:
  Server(pjs::Object *handler, const Options &options);
  Server(const std::function<Message*(Server*, Message*)> &handler, const Options &options);

  //
  // Server::Handler
  //

  class Handler :
    public pjs::ObjectTemplate<Handler, pjs::Promise::Callback>,
    public EventFunction
  {
  public:
    Handler(Server *server) : m_server(server) {}
    void tunnel(Pipeline *pipeline) { m_tunnel = pipeline; }

  private:
    Server* m_server;
    MessageReader m_message_reader;
    pjs::Ref<Pipeline> m_tunnel;

    virtual void on_event(Event *evt) override;
    virtual void on_resolved(const pjs::Value &value) override;
    virtual void on_rejected(const pjs::Value &error) override;

    friend class pjs::ObjectTemplate<Handler, pjs::Promise::Callback>;
  };

private:
  Server(const Server &r);
  ~Server();

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;

  virtual auto on_demux_open_stream() -> EventFunction* override;
  virtual void on_demux_close_stream(EventFunction *stream) override;
  virtual void on_demux_queue_dedicate(EventFunction *stream) override;

  pjs::Ref<pjs::Object> m_handler_obj;
  std::function<Message*(Server*, Message*)> m_handler_func;
};

//
// TunnelServer
//

class TunnelServer : public Filter {
public:
  TunnelServer(pjs::Function *handler);

private:
  TunnelServer(const TunnelServer &r);
  ~TunnelServer();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void chain() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<pjs::Function> m_handler;
  pjs::Ref<Pipeline> m_pipeline;
  MessageReader m_message_reader;
};

//
// TunnelClient
//

class TunnelClient : public Filter, public EventSource {
public:
  TunnelClient(pjs::Object *handshake);

private:
  TunnelClient(const TunnelClient &r);
  ~TunnelClient();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<pjs::Object> m_handshake;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<RequestHead> m_request_head;
  pjs::Ref<ResponseHead> m_response_head;
  pjs::Ref<StreamEnd> m_eos;
  Data m_buffer;
  bool m_is_tunnel_started = false;
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP
