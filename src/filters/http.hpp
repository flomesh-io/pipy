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

  bool is_final() const { return m_is_final; }
  bool is_bodiless() const { return m_is_bodiless; }
  bool is_connect() const { return m_is_connect; }
  bool is_upgrade_websocket() const { return m_is_upgrade_websocket; }
  bool is_upgrade_http2() const { return m_is_upgrade_http2; }

  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_connect(bool b) { m_is_connect = b; }

protected:
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
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_final = false;
  bool m_is_bodiless = false;
  bool m_is_connect = false;
  bool m_is_upgrade_websocket = false;
  bool m_is_upgrade_http2 = false;
  bool m_is_tunnel = false;

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

  bool is_bodiless() const { return m_is_bodiless; }
  bool is_connect() const { return m_is_connect; }
  bool is_upgrade_websocket() const { return m_is_upgrade_websocket; }
  bool is_upgrade_http2() const { return m_is_upgrade_http2; }

  void set_buffer_size(int size) { m_buffer_size = size; }
  void set_final(bool b) { m_is_final = b; }
  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_connect(bool b) { m_is_connect = b; }

private:
  pjs::Ref<MessageStart> m_start;
  Data m_buffer;
  int m_buffer_size = DATA_CHUNK_SIZE;
  int m_content_length = 0;
  bool m_chunked = false;
  bool m_is_response;
  bool m_is_final = false;
  bool m_is_bodiless = false;
  bool m_is_connect = false;
  bool m_is_upgrade_websocket = false;
  bool m_is_upgrade_http2 = false;
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
  virtual void dump(std::ostream &out) override;

  Decoder m_ef_decode;
};

//
// ResponseDecoder
//

class ResponseDecoder : public Filter {
public:
  ResponseDecoder(pjs::Object *options);

private:
  ResponseDecoder(const ResponseDecoder &r);
  ~ResponseDecoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  struct SetBodiless : public EventFunction {
    ResponseDecoder* filter;
    SetBodiless(ResponseDecoder *f) : filter(f) {}
    virtual void on_event(Event *evt) override {
      filter->on_set_bodiless(evt);
    }
  };

  Decoder m_ef_decode;
  SetBodiless m_ef_set_bodiless;
  pjs::Value m_bodiless;

  void on_set_bodiless(Event *evt);
};

//
// RequestEncoder
//

class RequestEncoder : public Filter {
public:
  RequestEncoder(pjs::Object *options);

private:
  RequestEncoder(const RequestEncoder &r);
  ~RequestEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  Encoder m_ef_encode;
  int m_buffer_size = DATA_CHUNK_SIZE;
};

//
// ResponseEncoder
//

class ResponseEncoder : public Filter {
public:
  ResponseEncoder(pjs::Object *options);

private:
  ResponseEncoder(const ResponseEncoder &r);
  ~ResponseEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  Encoder m_ef_encode;
  pjs::Value m_final;
  pjs::Value m_bodiless;
  int m_buffer_size = DATA_CHUNK_SIZE;
};

//
// RequestQueue
//

class RequestQueue : protected EventProxy {
protected:
  struct Request :
    public pjs::Pooled<Request>,
    public List<Request>::Item
  {
    bool is_final;
    bool is_bodiless;
    bool is_connect;
  };

  List<Request> m_queue;
  bool m_started = false;

  void reset();

  virtual void on_input(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void on_enqueue(Request *req) = 0;
  virtual void on_dequeue(Request *req) = 0;
};

//
// HTTP2Demuxer
//

class HTTP2Demuxer :
  public pjs::Pooled<HTTP2Demuxer>,
  public http2::Demuxer
{
public:
  HTTP2Demuxer(Filter *filter) : m_filter(filter) {}

  auto on_new_sub_pipeline() -> Pipeline* override {
    return m_filter->sub_pipeline(0, true);
  }

private:
  Filter* m_filter;
};

//
// Demux
//

class Demux :
  public Filter,
  public QueueDemuxer,
  protected RequestQueue,
  protected Decoder,
  protected Encoder
{
public:
  Demux(pjs::Object *options);

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  int m_buffer_size = DATA_CHUNK_SIZE;
  http2::Demuxer *m_http2_demuxer = nullptr;

  virtual auto on_new_sub_pipeline() -> Pipeline* override;
  virtual void on_enqueue(Request *req) override;
  virtual void on_dequeue(Request *req) override;
  virtual void on_http2_pass() override;

  void upgrade_http2();
};

//
// HTTP2Muxer
//

class HTTP2Muxer :
  public pjs::Pooled<HTTP2Muxer>,
  public http2::Muxer
{
};

//
// Mux
//

class Mux : public pipy::Mux {
public:
  Mux();
  Mux(const pjs::Value &key, pjs::Object *options);

private:
  Mux(const Mux &r);
  ~Mux();

  int m_version = 1;
  int m_buffer_size = DATA_CHUNK_SIZE;

  virtual auto clone() -> Filter* override;
  virtual void dump(std::ostream &out) override;
  virtual auto on_new_session() -> MuxBase::Session* override;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, MuxBase::Session>,
    public QueueMuxer,
    protected RequestQueue,
    protected Encoder,
    protected Decoder
  {
    Session(int version, int buffer_size)
      : Encoder(false)
      , Decoder(true)
      , m_version(version)
      , m_buffer_size(buffer_size) {}

    virtual void open() override;
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;
    virtual void close() override;
    virtual void on_enqueue(Request *req) override;
    virtual void on_dequeue(Request *req) override;

    int m_version;
    int m_buffer_size;
    HTTP2Muxer* m_http2_muxer = nullptr;

    void upgrade_http2();

    friend class Mux;
  };
};

//
// Server
//

class Server : public Filter {
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
  virtual void dump(std::ostream &out) override;

  class Handler :
    public pjs::Pooled<Handler>,
    public EventFunction
  {
    Handler(Server *server)
      : m_server(server) {}

    Server* m_server;
    pjs::Ref<MessageStart> m_start;
    Data m_buffer;

    virtual void on_event(Event *evt) override;

    friend class Server;
  };

  std::function<Message*(Message*)> m_handler_func;
  pjs::Ref<pjs::Object> m_handler_obj;
  Decoder m_ef_decoder;
  Encoder m_ef_encoder;
  Handler m_ef_handler;
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
  virtual void dump(std::ostream &out) override;

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
  TunnelClient(const pjs::Value &target);

private:
  TunnelClient(const TunnelClient &r);
  ~TunnelClient();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  void on_receive(Event *evt);

  pjs::Value m_target;
  pjs::Ref<Pipeline> m_pipeline;
  bool m_is_tunneling = false;

  friend class TunnelClientReceiver;
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP
