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

  bool is_bodiless() const { return m_is_bodiless; }
  bool is_final() const { return m_is_final; }

  void set_bodiless(bool b) { m_is_bodiless = b; }

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
  };

  State m_state = HEAD;
  Data m_head_buffer;
  pjs::Ref<MessageHead> m_head;
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_final = false;

  virtual void on_event(Event *evt) override;

  void message_start() {
    output(MessageStart::make(m_head));
  }

  void message_end() {
    output(MessageEnd::make());
    m_is_bodiless = false;
    m_is_final = false;
  }

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

  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_final(bool b) { m_is_final = b; }

private:
  pjs::Ref<MessageStart> m_start;
  pjs::Ref<Data> m_buffer;
  int m_content_length = -1;
  bool m_chunked = false;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_final = false;

  virtual void on_event(Event *evt) override;

  void output_head();

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
  RequestEncoder();

private:
  RequestEncoder(const RequestEncoder &r);
  ~RequestEncoder();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  Encoder m_ef_encode;
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
  pjs::Value m_bodiless;
};

//
// RequestEnqueue
//

struct RequestEnqueue : public EventFunction {
  virtual void on_event(Event *evt) override;
};

//
// RequestDequeue
//

struct RequestDequeue : public EventFunction {
  virtual void on_event(Event *evt) override;
};

//
// RequestQueue
//

class RequestQueue :
  protected RequestEnqueue,
  protected RequestDequeue
{
protected:
  struct Request :
    public pjs::Pooled<Request>,
    public List<Request>::Item
  {
    bool is_bodiless;
    bool is_final;
  };

  List<Request> m_queue;

  void reset();

  virtual void on_enqueue(Request *req) = 0;
  virtual void on_dequeue(Request *req) = 0;

  struct Enqueue : public EventFunction {
    RequestQueue* queue;
    Enqueue(RequestQueue *q) : queue(q) {}
    virtual void on_event(Event *evt) override;
  };

  struct Dequeue : public EventFunction {
    RequestQueue* queue;
    Dequeue(RequestQueue *q) : queue(q) {}
    virtual void on_event(Event *evt) override;
  };

  friend class RequestEnqueue;
  friend class RequestDequeue;
};

//
// Demux
//

class Demux :
  public Filter,
  public DemuxFunction,
  protected RequestQueue
{
public:
  Demux();

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  Decoder m_ef_decoder;
  Encoder m_ef_encoder;

  virtual auto on_new_sub_pipeline() -> Pipeline* override;
  virtual void on_enqueue(Request *req) override;
  virtual void on_dequeue(Request *req) override;
};

//
// Mux
//

class Mux : public pipy::Mux {
public:
  Mux();
  Mux(const pjs::Value &key);

private:
  Mux(const Mux &r);
  ~Mux();

  virtual auto clone() -> Filter* override;
  virtual void dump(std::ostream &out) override;

  //
  // Mux::Session
  //

  class Session :
    public pjs::Pooled<Session, pipy::Mux::Session>,
    protected RequestQueue
  {
  public:
    Session()
      : m_ef_encoder(false)
      , m_ef_decoder(true) {}

  private:
    Encoder m_ef_encoder;
    Decoder m_ef_decoder;

    virtual void open(Pipeline *pipeline) override;
    virtual void input(Event *evt) override;
    virtual void on_enqueue(Request *req) override;
    virtual void on_dequeue(Request *req) override;
    virtual void close() override;

    friend class Stream;
  };

  virtual auto on_new_session() -> Session* override {
    return new Session();
  }
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

} // namespace http
} // namespace pipy

#endif // HTTP_HPP
