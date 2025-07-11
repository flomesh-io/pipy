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
#include "timer.hpp"

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
  void set_max_header_size(size_t size) { m_max_header_size = size; }
  void set_tunnel() { m_is_tunnel = true; }

protected:
  virtual void on_decode_message_start(RequestHead *head) {}
  virtual auto on_decode_message_start(ResponseHead *head) -> RequestHead* { return nullptr; }
  virtual void on_decode_message_end(MessageTail *tail) {}
  virtual bool on_decode_tunnel(TunnelType tt) { return false; }
  virtual void on_decode_final() {}
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
  int m_current_size = 0;
  int m_head_size = 0;
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
  Encoder(bool is_response, std::shared_ptr<BufferStats> buffer_stats = nullptr);

  void reset();
  void set_buffer_size(int size) { m_buffer_size = size; }
  void set_tunnel() { m_is_tunnel = true; }

protected:
  virtual auto on_encode_message_start(ResponseHead *head, bool &is_final) -> RequestHead* { return nullptr; }
  virtual bool on_encode_tunnel(TunnelType tt) { return false; }

private:
  DataBuffer m_buffer;
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
};

//
// ResponseDecoder
//

class ResponseDecoder : public Filter, public Decoder {
public:
  struct Options : pipy::Options {
    pjs::Ref<pjs::Function> on_message_start_f;
    Options() {}
    Options(pjs::Object *options);
  };

  ResponseDecoder(const Options &options);

private:
  ResponseDecoder(const ResponseDecoder &r);
  ~ResponseDecoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
  pjs::Ref<RequestHead> m_request_head;

  virtual auto on_decode_message_start(ResponseHead *head) -> RequestHead* override;
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

  RequestEncoder(const Options &options);

private:
  RequestEncoder(const RequestEncoder &r);
  ~RequestEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
};

//
// ResponseEncoder
//

class ResponseEncoder : public Filter, public Encoder {
public:
  struct Options : pipy::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    pjs::Ref<pjs::Function> on_message_start_f;
    Options() {}
    Options(pjs::Object *options);
  };

  ResponseEncoder(const Options &options);

private:
  ResponseEncoder(const ResponseEncoder &r);
  ~ResponseEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
  pjs::Ref<RequestHead> m_request_head;

  virtual auto on_encode_message_start(ResponseHead *head, bool &is_final) -> RequestHead* override;
};

//
// Demux
//

class Demux :
  public Filter,
  protected Decoder,
  protected Encoder,
  protected http2::Server
{
public:
  struct Options : public http2::Endpoint::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    size_t max_header_size = DATA_CHUNK_SIZE;
    int max_messages = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  Demux(const Options &options);

protected:
  Demux(const Demux &r);
  ~Demux();

  //
  // Demux::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public List<Stream>::Item,
    public EventTarget
  {
  public:
    Stream(Demux *demux, EventFunction *handler, RequestHead *head);
    ~Stream();

    auto handler() const -> EventFunction* { return m_handler; }
    auto head() const -> RequestHead* { return m_head; }

  private:
    virtual void on_event(Event *evt) override;

    Demux* m_demux;
    EventFunction* m_handler;
    pjs::Ref<RequestHead> m_head;
    EventBuffer m_buffer;
    bool m_started = false;
    bool m_ended = false;
    bool m_continue = false;
  };

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void shutdown() override;
  virtual void dump(Dump &d) override;

  Options m_options;
  List<Stream> m_streams;
  int m_message_count = 0;
  bool m_is_http2 = false;
  bool m_is_tunnel = false;
  bool m_has_shutdown = false;

  virtual auto on_server_open_stream() -> EventFunction* override;
  virtual void on_server_close_stream(EventFunction *stream) override;
  virtual void on_decode_message_start(RequestHead *head) override;
  virtual void on_decode_message_end(MessageTail *tail) override;
  virtual bool on_decode_tunnel(TunnelType tt) override;
  virtual void on_decode_error() override;
  virtual auto on_encode_message_start(ResponseHead *head, bool &is_final) -> RequestHead* override;
  virtual bool on_encode_tunnel(TunnelType tt) override;

  void clear_streams();
};

//
// Mux
//

class Mux : public Filter, public EventSource, public Ticker::Watcher {
public:
  struct Options :
    public Muxer::Options,
    public http2::Endpoint::Options
  {
    size_t buffer_size = DATA_CHUNK_SIZE;
    size_t max_header_size = DATA_CHUNK_SIZE;
    int version = 1;
    double timeout = 0;
    pjs::Ref<pjs::Function> timeout_f;
    pjs::Ref<pjs::Str> version_s;
    pjs::Ref<pjs::Function> version_f;
    pjs::Ref<pjs::Function> ping_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const Options &options);

private:
  Mux(const Mux &r);

  class HTTPStream;
  class HTTPQueue;
  class HTTPSession;
  class HTTPMuxer;

  //
  // Mux::HTTPStream
  //

  class HTTPStream :
    public pjs::RefCount<HTTPStream>,
    public pjs::Pooled<HTTPStream>,
    public Muxer::Stream,
    public EventFunction
  {
  public:
    void set_tunnel() { m_is_tunnel = true; }
    void discard();

  private:
    HTTPStream() {}
    ~HTTPStream() {}

    void open(bool is_http2);

    virtual void on_event(Event *evt) override;

    EventFunction* m_http2_stream = nullptr;
    EventBuffer m_buffer;
    pjs::Ref<RequestHead> m_head;
    bool m_is_http2 = false;
    bool m_is_open = false;
    bool m_is_sending = false;
    bool m_is_tunnel = false;
    bool m_started = false;
    bool m_ended = false;

    friend class pjs::RefCount<HTTPStream>;
    friend class HTTPSession;
    friend class HTTPQueue;
  };

  //
  // Mux::HTTPQueue
  //

  class HTTPQueue :
    public Muxer::Session,
    public EventTarget
  {
  public:
    auto alloc(EventTarget::Input *output) -> HTTPStream*;

  protected:
    HTTPQueue() {}
    ~HTTPQueue() {}

    void open(bool is_http2);
    void free(HTTPStream *s);
    void free_all();
    auto current_request() -> RequestHead*;
    void set_tunnel() { m_is_tunnel = true; }

  private:
    bool m_is_http2 = false;
    bool m_is_open = false;
    bool m_is_tunnel = false;
    bool m_started = false;
    bool m_continue = false;

    virtual void on_event(Event *evt) override;
  };

  //
  // Mux::HTTPSession
  //

  class HTTPSession :
    public pjs::RefCount<HTTPSession>,
    public pjs::Pooled<HTTPSession>,
    public HTTPQueue,
    public Encoder,
    protected Decoder,
    protected http2::Client
  {
    HTTPSession(Mux *mux);
    ~HTTPSession();

    void free_all();
    void close();

    int m_version = 0;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<pjs::Promise::Callback> m_version_callback;
    pjs::Ref<Context> m_context;
    pjs::Ref<pjs::Function> m_ping_handler;
    pjs::Ref<pjs::Promise::Callback> m_ping_callback;

    void select_protocol(const pjs::Value &version);
    void schedule_ping(Data *ack = nullptr);

    virtual auto on_decode_message_start(ResponseHead *head) -> RequestHead* override;
    virtual bool on_decode_tunnel(TunnelType tt) override;
    virtual void on_ping(const Data &data) override;

    friend class pjs::RefCount<HTTPSession>;
    friend class HTTPStream;
    friend class HTTPMuxer;
  };

  //
  // Mux::HTTPMuxer
  //

  class HTTPMuxer : public Muxer {
  public:
    HTTPMuxer();
    HTTPMuxer(const Options &options);

  private:
    Options m_options;

    virtual auto on_muxer_session_open(Filter *filter) -> Session* override;
    virtual void on_muxer_session_close(Session *session) override;
  };

  pjs::Ref<HTTPMuxer> m_muxer;
  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Ref<HTTPSession> m_session;
  pjs::Ref<HTTPStream> m_stream;
  Options m_options;
  double m_timeout = 0;
  double m_start_time = 0;
  bool m_has_error = false;

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
  virtual void on_reply(Event *evt) override;
  virtual void on_tick(double tick) override;
};

//
// Server
//

class Server : public Demux {
public:
  Server(pjs::Object *handler, const Options &options);

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

  virtual auto on_server_open_stream() -> EventFunction* override;
  virtual void on_server_close_stream(EventFunction *stream) override;

  pjs::Ref<pjs::Object> m_handler;
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
  pjs::Ref<RequestHead> m_request_head;
  pjs::Ref<pjs::Promise::Callback> m_promise_callback;
  EventBuffer m_buffer;
  MessageReader m_message_reader;

  void on_resolve(pjs::Promise::State state, const pjs::Value &value);
  void start_tunnel(Message *response);
};

//
// TunnelClient
//

class TunnelClient : public Filter, public EventSource {
public:
  enum class State {
    idle,
    connecting,
    connected,
    closed,
  };

  struct Options : public pipy::Options {
    pjs::Ref<pjs::Function> on_state_f;
    Options() {}
    Options(pjs::Object *options);
  };

  TunnelClient(pjs::Object *handshake);
  TunnelClient(pjs::Object *handshake, const Options &options);

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
  Options m_options;
  std::function<void(State state)> m_on_state_change;
  bool m_is_tunnel_started = false;
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP
