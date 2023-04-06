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
// Decoder
//

class Decoder : public EventFunction {
public:
  Decoder(bool is_response)
    : m_is_response(is_response) {}

  void reset();
  bool has_error() const { return m_has_error; }
  void set_tunnel() { m_is_tunnel = true; }

protected:
  virtual void on_decode_request(RequestHead *head) {}
  virtual auto on_decode_response(ResponseHead *head) -> RequestHead* { return nullptr; }
  virtual bool on_decode_tunnel(TunnelType tt) { return false; }
  virtual void on_decode_error() {}

private:
  const static int MAX_HEADER_SIZE = 0x1000;

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
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Str> m_header_transfer_encoding;
  pjs::Ref<pjs::Str> m_header_content_length;
  TunnelType m_responded_tunnel_type = TunnelType::NONE;
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_tunnel = false;
  bool m_has_error = false;

  virtual void on_event(Event *evt) override;

  void message_start();
  void message_end();
  void stream_end(StreamEnd *end);

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
  virtual void on_encode_request(RequestHead *head) {}
  virtual auto on_encode_response(ResponseHead *head) -> RequestHead* { return nullptr; }
  virtual bool on_encode_tunnel(TunnelType tt) { return false; }

private:
  Data m_buffer;
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Str> m_protocol;
  pjs::Ref<pjs::Str> m_method;
  pjs::Ref<pjs::Str> m_path;
  TunnelType m_responded_tunnel_type = TunnelType::NONE;
  int m_buffer_size = DATA_CHUNK_SIZE;
  int m_status_code = 0;
  int m_content_length = 0;
  bool m_chunked = false;
  bool m_is_response;
  bool m_is_final = false;
  bool m_is_bodiless = false;
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

  virtual void on_decode_request(RequestHead *head) override;
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

  virtual auto on_decode_response(ResponseHead *head) -> RequestHead* override;
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

  virtual void on_encode_request(RequestHead *head) override;
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

  virtual auto on_encode_response(ResponseHead *head) -> RequestHead* override;
};

//
// RequestQueue
//

class RequestQueue {
public:
  bool empty() const { return m_queue.empty(); }
  void reset();
  void push(RequestHead *head);
  auto head() const -> RequestHead*;
  auto shift() -> RequestHead*;

private:
  struct Request :
    public pjs::Pooled<Request>,
    public List<Request>::Item
  {
    pjs::Ref<RequestHead> head;
  };

  List<Request> m_queue;
};

//
// Demux
//

class Demux :
  public Filter,
  protected Demuxer,
  protected Demuxer::Queue,
  protected Decoder,
  protected Encoder
{
public:
  struct Options : public http2::Endpoint::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    Options() {}
    Options(pjs::Object *options);
  };

  Demux(const Options &options);

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void shutdown() override;
  virtual void dump(Dump &d) override;

  //
  // Demux::HTTP2Demuxer
  //

  class HTTP2Demuxer :
    public pjs::Pooled<HTTP2Demuxer>,
    public http2::Server
  {
  public:
    HTTP2Demuxer(Demux *demux)
      : http2::Server(demux->m_options)
      , m_demux(demux) {}

    auto on_new_stream_pipeline(Input *chain_to) -> PipelineBase* override {
      return m_demux->sub_pipeline(0, true, chain_to);
    }

  private:
    Demux* m_demux;
  };

  Options m_options;
  RequestQueue m_request_queue;
  HTTP2Demuxer* m_http2_demuxer = nullptr;
  bool m_shutdown = false;

  virtual auto on_queue_message(MessageStart *start) -> int override;
  virtual auto on_open_stream() -> EventFunction* override;
  virtual void on_close_stream(EventFunction *stream) override;
  virtual void on_decode_error() override;
  virtual void on_decode_request(RequestHead *head) override;
  virtual auto on_encode_response(ResponseHead *head) -> RequestHead* override;
  virtual bool on_decode_tunnel(TunnelType tt) override;
  virtual bool on_encode_tunnel(TunnelType tt) override;

  void upgrade_http2();
};

//
// HTTP2Muxer
//

class HTTP2Muxer :
  public pjs::Pooled<HTTP2Muxer>,
  public http2::Client
{
public:
  using http2::Client::Client;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  struct Options :
    public MuxBase::Options,
    public http2::Endpoint::Options
  {
    size_t buffer_size = DATA_CHUNK_SIZE;
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

private:
  Mux(const Mux &r);
  ~Mux();

  Options m_options;
  EventBuffer m_waiting_events;

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual auto on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* override;

  auto verify_http_version(int version) -> int;
  auto verify_http_version(pjs::Str *name) -> int;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxBase::Session>,
    public Muxer::Queue,
    protected Encoder,
    protected Decoder
  {
    Session(const Options &options)
      : Encoder(false)
      , Decoder(true)
      , m_options(options) {}

    ~Session();

    virtual void open(Muxer *muxer) override;
    virtual auto open_stream(Muxer *muxer) -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;
    virtual void close() override;
    virtual bool should_continue(Muxer *muxer) override;
    virtual void on_encode_request(RequestHead *head) override;
    virtual auto on_decode_response(ResponseHead *head) -> RequestHead* override;
    virtual bool on_decode_tunnel(TunnelType tt) override;
    virtual void on_decode_error() override;

    const Options& m_options;
    int m_version_selected = 0;
    RequestQueue m_request_queue;
    HTTP2Muxer* m_http2_muxer = nullptr;

    bool select_protocol(Muxer *muxer);
    void upgrade_http2();

    friend class Mux;
  };

  //
  // Mux::SessionCluster
  //

  class SessionCluster : public pjs::Pooled<SessionCluster, MuxBase::SessionCluster> {
    SessionCluster(const Options &options)
      : pjs::Pooled<SessionCluster, MuxBase::SessionCluster>(options)
      , m_options(options) {}

    virtual auto session() -> MuxBase::Session* override { return new Session(m_options); }
    virtual void free() override { delete this; }

    Options m_options;

    friend class Mux;
  };
};

//
// Server
//

class Server :
  public Filter,
  protected Decoder,
  protected Encoder
{
public:
  Server(const std::function<Message*(Server*, Message*)> &handler);
  Server(pjs::Object *handler);

private:
  Server(const Server &r);
  ~Server();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void shutdown() override;
  virtual void dump(Dump &d) override;

  virtual void on_decode_error() override;
  virtual void on_decode_request(RequestHead *head) override;
  virtual auto on_encode_response(ResponseHead *head) -> RequestHead* override;
  virtual bool on_decode_tunnel(TunnelType tt) override;
  virtual bool on_encode_tunnel(TunnelType tt) override;

  //
  // Server::Handler
  //

  class Handler :
    public pjs::Pooled<Handler>,
    public PipelineBase
  {
  public:
    Handler(Server *server)
      : m_server(server) {}

    void reset();

  private:
    Server* m_server;
    MessageReader m_message_reader;

    virtual void on_event(Event *evt) override;
    virtual void on_recycle() override { delete static_cast<Handler*>(this); }
  };

  //
  // Server::HTTP2Server
  //

  class HTTP2Server :
    public pjs::Pooled<HTTP2Server>,
    public http2::Server
  {
  public:
    HTTP2Server(http::Server *server)
      : http2::Server(http2::Server::Options())
      , m_server(server) {}

  private:
    http::Server* m_server;

    auto on_new_stream_pipeline(Input *chain_to) -> PipelineBase* override {
      auto handler = new Handler(m_server);
      handler->chain(chain_to);
      return handler;
    }
  };

  std::function<Message*(Server*, Message*)> m_handler_func;
  pjs::Ref<pjs::Object> m_handler_obj;
  pjs::Ref<Handler> m_handler;
  RequestQueue m_request_queue;
  pjs::Ref<Pipeline> m_tunnel;
  HTTP2Server* m_http2_server = nullptr;
  bool m_shutdown = false;

  void upgrade_http2();
  void on_tunnel_data(Data *data);
  void on_tunnel_end(StreamEnd *end);
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
  virtual void dump(Dump &d) override;
  virtual void on_reply(Event *evt) override;

  pjs::Ref<pjs::Object> m_handshake;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<RequestHead> m_request_head;
  pjs::Ref<ResponseHead> m_response_head;
  Data m_buffer;
  bool m_is_tunnel_started = false;

  friend class TunnelClientReceiver;
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP
