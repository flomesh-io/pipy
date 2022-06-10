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

  auto header_connection() const -> pjs::Str* { return m_header_connection; }
  auto header_upgrade() const -> pjs::Str* { return m_header_upgrade; }
  bool has_error() const { return m_has_error; }

  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_tunnel(bool b) { m_is_tunnel = b; }

protected:
  virtual void on_decode_request(http::RequestHead *head) {}
  virtual void on_decode_response(http::ResponseHead *head) {}
  virtual void on_decode_error() {}
  virtual void on_http2_pass() {}

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
  Data m_buffer;
  Data m_head_buffer;
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Str> m_header_transfer_encoding;
  pjs::Ref<pjs::Str> m_header_content_length;
  pjs::Ref<pjs::Str> m_header_connection;
  pjs::Ref<pjs::Str> m_header_upgrade;
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_tunnel = false;
  bool m_has_error = false;

  virtual void on_event(Event *evt) override;

  void message_start();
  void message_end();
  void stream_end(StreamEnd *end);

  bool is_bodiless_response() const {
    return m_is_response && m_is_bodiless;
  }
};

//
// Encoder
//

class Encoder : public EventFunction {
public:
  Encoder(bool is_response)
    : m_is_response(is_response) {}

  void reset();

  auto protocol() const -> pjs::Str* { return m_protocol; }
  auto method() const -> pjs::Str* { return m_method; }
  auto header_connection() const -> pjs::Str* { return m_header_connection; }
  auto header_upgrade() const -> pjs::Str* { return m_header_upgrade; }

  void set_buffer_size(int size) { m_buffer_size = size; }
  void set_final(bool b) { m_is_final = b; }
  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_tunnel(bool b) { m_is_tunnel = b; }

protected:
  virtual void on_encode_request(pjs::Object *head) {}
  virtual void on_encode_response(pjs::Object *head) {}

private:
  pjs::Ref<MessageStart> m_start;
  pjs::Ref<pjs::Str> m_protocol;
  pjs::Ref<pjs::Str> m_method;
  pjs::Ref<pjs::Str> m_header_connection;
  pjs::Ref<pjs::Str> m_header_upgrade;
  Data m_buffer;
  int m_buffer_size = DATA_CHUNK_SIZE;
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

  bool is_bodiless_response() const {
    return m_is_response && m_is_bodiless;
  }

  static Data::Producer s_dp;
};

//
// RequestDecoder
//

class RequestDecoder : public Filter {
public:
  RequestDecoder();

private:
  RequestDecoder(const RequestDecoder &r);
  ~RequestDecoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Decoder m_ef_decode;
};

//
// ResponseDecoder
//

class ResponseDecoder : public Filter {
public:
  struct Options : public pipy::Options {
    bool bodiless = false;
    pjs::Ref<pjs::Function> bodiless_f;
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

  struct SetBodiless : public EventFunction {
    ResponseDecoder* filter;
    SetBodiless(ResponseDecoder *f) : filter(f) {}
    virtual void on_event(Event *evt) override {
      filter->on_set_bodiless(evt);
      output(evt);
    }
  };

  Decoder m_ef_decode;
  SetBodiless m_ef_set_bodiless;
  Options m_options;

  void on_set_bodiless(Event *evt);
};

//
// RequestEncoder
//

class RequestEncoder : public Filter {
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

  Encoder m_ef_encode;
  Options m_options;
};

//
// ResponseEncoder
//

class ResponseEncoder : public Filter {
public:
  struct Options : pipy::Options {
    bool final;
    bool bodiless;
    pjs::Ref<pjs::Function> final_f;
    pjs::Ref<pjs::Function> bodiless_f;
    size_t buffer_size = DATA_CHUNK_SIZE;
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

  Encoder m_ef_encode;
  Options m_options;
};

//
// RequestQueue
//

class RequestQueue {
public:
  struct Request :
    public pjs::Pooled<Request>,
    public List<Request>::Item
  {
    pjs::Ref<pjs::Str> protocol;
    pjs::Ref<pjs::Str> method;
    pjs::Ref<pjs::Str> header_connection;
    pjs::Ref<pjs::Str> header_upgrade;
    bool is_final() const;
    bool is_bodiless() const;
    bool is_switching() const;
    bool is_http2() const;
  };

  bool empty() const { return m_queue.empty(); }
  void reset();
  void shutdown();
  void push(Request *req);
  auto shift() -> Request*;

private:
  List<Request> m_queue;
};

//
// Demux
//

class Demux :
  public Filter,
  public QueueDemuxer,
  protected Decoder,
  protected Encoder
{
public:
  struct Options : public pipy::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    Options();
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
  // Muxer::HTTP2Demuxer
  //

  class HTTP2Demuxer :
    public pjs::Pooled<HTTP2Demuxer>,
    public http2::Server
  {
  public:
    HTTP2Demuxer(Filter *filter) : m_filter(filter) {}

    auto on_new_stream_pipeline() -> PipelineBase* override {
      return m_filter->sub_pipeline(0, true);
    }

  private:
    Filter* m_filter;
  };

  Options m_options;
  RequestQueue m_request_queue;
  HTTP2Demuxer* m_http2_demuxer = nullptr;

  virtual auto on_new_sub_pipeline() -> Pipeline* override;
  virtual void on_decode_error() override;
  virtual void on_decode_request(http::RequestHead *head) override;
  virtual void on_encode_response(pjs::Object *head) override;
  virtual void on_http2_pass() override;

  void upgrade_http2();
};

//
// HTTP2Muxer
//

class HTTP2Muxer :
  public pjs::Pooled<HTTP2Muxer>,
  public http2::Client
{
};

//
// Mux
//

class Mux : public pipy::MuxQueue {
public:
  struct Options : public pipy::MuxQueue::Options {
    size_t buffer_size = DATA_CHUNK_SIZE;
    int version = 1;
    pjs::Ref<pjs::Function> version_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Mux();
  Mux(pjs::Function *group, const Options &options);

private:
  Mux(const Mux &r);
  ~Mux();

  Options m_options;

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual auto on_new_session() -> MuxBase::Session* override;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxBase::Session>,
    public QueueMuxer,
    protected Encoder,
    protected Decoder,
    public ContextGroup::Waiter
  {
    Session(int buffer_size, int version, pjs::Function *version_f)
      : Encoder(false)
      , Decoder(true)
      , m_buffer_size(buffer_size)
      , m_version(version)
      , m_version_f(version_f) {}

    ~Session();

    virtual void open() override;
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;
    virtual void close() override;
    virtual void on_encode_request(pjs::Object *head) override;
    virtual void on_decode_response(http::ResponseHead *head) override;
    virtual void on_decode_error() override;
    virtual void on_notify(Context *ctx) override;

    int m_buffer_size;
    int m_version;
    int m_version_selected = 0;
    pjs::Ref<pjs::Function> m_version_f;
    RequestQueue m_request_queue;
    HTTP2Muxer* m_http2_muxer = nullptr;

    void select_protocol();
    void upgrade_http2();

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
  Server(const std::function<Message*(Message*)> &handler);
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
  virtual void on_decode_request(http::RequestHead *head) override;
  virtual void on_encode_response(pjs::Object *head) override;
  virtual void on_http2_pass() override;

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

  private:
    Server* m_server;
    pjs::Ref<MessageStart> m_start;
    Data m_buffer;

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
    HTTP2Server(http::Server *server) : m_server(server) {}

  private:
    http::Server* m_server;

    auto on_new_stream_pipeline() -> PipelineBase* override {
      return new Handler(m_server);
    }
  };

  std::function<Message*(Message*)> m_handler_func;
  pjs::Ref<pjs::Object> m_handler_obj;
  pjs::Ref<Handler> m_handler;
  RequestQueue m_request_queue;
  pjs::Ref<Pipeline> m_tunnel;
  HTTP2Server* m_http2_server = nullptr;
  bool m_switching = false;

  void upgrade_http2();
  void start_tunnel();
  void on_tunnel_data(Data *data);
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
  pjs::Ref<MessageStart> m_start;
  Data m_buffer;
};

//
// TunnelClient
//

class TunnelClientReceiver : public EventTarget {
  virtual void on_event(Event *evt) override;
};

class TunnelClient : public Filter, public TunnelClientReceiver {
public:
  TunnelClient(const pjs::Value &handshake);

private:
  TunnelClient(const TunnelClient &r);
  ~TunnelClient();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  void on_receive(Event *evt);

  pjs::Value m_handshake;
  pjs::Ref<Pipeline> m_pipeline;
  bool m_is_tunneling = false;

  friend class TunnelClientReceiver;
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP
