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
#include "scarce.hpp"
#include "table.hpp"
#include "list.hpp"
#include "deframer.hpp"

namespace pipy {
namespace fcgi {

class RequestHead : public pjs::ObjectTemplate<RequestHead> {
public:
  int role = 1;
  bool keepAlive = false;
  pjs::Ref<pjs::Object> params;
};

class ResponseTail : public pjs::ObjectTemplate<ResponseTail> {
public:
  int appStatus = 0;
  int protocolStatus = 0;
  pjs::Ref<Data> stderr_data;
};

//
// ParamDecoder
//

class ParamDecoder : public Deframer {
public:
  ParamDecoder(pjs::Object *output = nullptr) { reset(output); }

  void reset(pjs::Object *output);

private:
  enum State {
    STATE_NAME_LEN,
    STATE_NAME_LEN32,
    STATE_VALUE_LEN,
    STATE_VALUE_LEN32,
    STATE_NAME,
    STATE_VALUE,
  };

  virtual auto on_state(int state, int c) -> int override;

  pjs::Ref<pjs::Object> m_params;
  pjs::Ref<Data> m_name;
  pjs::Ref<Data> m_value;
  int m_name_length;
  int m_value_length;
  uint8_t m_buffer[4];

  auto start_name() -> int;
  auto start_value() -> int;
  auto end_value() -> int;
};

//
// Endpoint
//

class Endpoint : public Deframer, public FlushTarget {
protected:
  Endpoint() { reset(); }

  //
  // Endpoint::Request
  //

  class Request : public List<Request>::Item {
  public:
    auto id() const -> int { return m_id; }

  protected:
    Request(int id)
      : m_id(id) {}

  private:
    int m_id;
  };

  virtual void on_record(int type, int request_id, Data &body) = 0;
  virtual auto on_new_request(int id) -> Request* = 0;
  virtual void on_delete_request(Request *request) = 0;
  virtual void on_output(Event *evt) = 0;

  void reset();
  auto request(int id) -> Request*;
  auto request_open(int id) -> Request*;
  void request_close(Request *request);
  void process_event(Event *evt);
  void send_record(int type, int request_id, const void *body, size_t size);
  void send_record(int type, int request_id, Data &body);
  void send_end();
  void shutdown();

private:
  enum State {
    STATE_RECORD_HEADER,
    STATE_RECORD_BODY,
  };

  void write_record_header(
    Data::Builder &db,
    int type,
    int request_id,
    int length,
    int &padding
  );

  List<Request> m_requests;
  ScarcePointerArray<Request> m_request_map;
  uint8_t m_header[8];
  int m_decoding_record_type;
  int m_decoding_request_id;
  int m_decoding_padding_length;
  pjs::Ref<Data> m_decoding_buffer;
  Data m_sending_buffer;
  bool m_sending_eos = false;
  bool m_sending_ended = false;

  virtual auto on_state(int state, int c) -> int override;
  virtual void on_flush() override;
};

//
// Client
//

class Client : public Endpoint, public EventSource {
public:
  auto open_request() -> EventFunction*;
  void close_request(EventFunction *request);
  void shutdown();

protected:
  virtual void on_record(int type, int request_id, Data &body) override;
  virtual auto on_new_request(int id) -> Endpoint::Request* override;
  virtual void on_delete_request(Endpoint::Request *request) override;
  virtual void on_output(Event *evt) override;
  virtual void on_reply(Event *evt) override;

  //
  // Client::Request
  //

  class Request :
    public pjs::Pooled<Request>,
    public Endpoint::Request,
    public EventFunction
  {
  public:
    Request(Client *client, int id) : Endpoint::Request(id), m_client(client) {}

    void receive_end(Data &data);
    void receive_stdout(Data &data);
    void receive_stderr(Data &data);

  private:
    Client* m_client;
    bool m_request_started = false;
    bool m_request_ended = false;
    bool m_response_started = false;
    bool m_response_ended = false;
    bool m_response_stdout_ended = false;
    bool m_response_stderr_ended = false;
    Data m_stderr_buffer;

    virtual void on_event(Event *evt) override;
  };

  Table<Request*> m_request_id_pool;
};

//
// Server
//

class Server : public Endpoint, public EventFunction {
public:
  void reset();
  void shutdown();

protected:
  virtual auto on_server_open_stream() -> EventFunction* = 0;
  virtual void on_server_close_stream(EventFunction *stream) = 0;
  virtual void on_server_complete() = 0;

  virtual void on_event(Event *evt) override;
  virtual void on_record(int type, int request_id, Data &body) override;
  virtual auto on_new_request(int id) -> Endpoint::Request* override;
  virtual void on_delete_request(Endpoint::Request *request) override;

  //
  // Server::Request
  //

  class Request : public pjs::Pooled<Request>, public Endpoint::Request, public EventTarget {
  public:
    Request(Server *server, int id) : Endpoint::Request(id), m_server(server) {}
    ~Request();

    void receive_begin(Data &data);
    void receive_abort();
    void receive_params(Data &data);
    void receive_stdin(Data &data);
    void receive_data(Data &data);

  private:
    Server* m_server;
    EventFunction* m_stream = nullptr;
    int m_role;
    int m_flags;
    bool m_keep_conn;
    pjs::Ref<pjs::Object> m_params;
    ParamDecoder m_param_decoder;
    bool m_request_started = false;
    bool m_request_ended = false;
    bool m_response_started = false;
    bool m_response_ended = false;

    virtual void on_event(Event *evt) override;
  };
};

//
// Demux
//

class Demux : public Filter, public Server {
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

  virtual auto on_server_open_stream() -> EventFunction* override;
  virtual void on_server_close_stream(EventFunction *stream) override;
  virtual void on_server_complete() override;
  virtual void on_output(Event *evt) override;
};

//
// Mux
//

class Mux : public Filter {
public:
  Mux(pjs::Function *session_selector);
  Mux(pjs::Function *session_selector, const Muxer::Options &options);

private:
  Mux(const Mux &mux);

  class Request;
  class Queue;
  class Pool;

  //
  // Mux::Request
  //

  class Request :
    public pjs::RefCount<Request>,
    public pjs::Pooled<Request>,
    public Muxer::Stream
  {
  public:
    void input(Event *evt);
    void discard();

  private:
    Request(Client *client, EventTarget::Input *output);
    ~Request();

    EventFunction* m_request = nullptr;

    friend class pjs::RefCount<Request>;
    friend class Queue;
  };

  //
  // Mux::Queue
  //

  class Queue :
    public pjs::RefCount<Queue>,
    public pjs::Pooled<Queue>,
    public Muxer::Session,
    public Client
  {
  public:
    auto alloc(EventTarget::Input *output) -> Mux::Request*;

  protected:
    Queue(Mux *mux);
    ~Queue() {}

    void free(Mux::Request *r);
    void free_all();

  private:
    pjs::Ref<Pipeline> m_pipeline;

    friend class pjs::RefCount<Queue>;
    friend class Request;
    friend class Pool;
  };

  //
  // Mux::Pool
  //

  class Pool : public Muxer {
  public:
    Pool();
    Pool(const Options &options);

  private:
    virtual auto on_muxer_session_open(Filter *filter) -> Session* override;
    virtual void on_muxer_session_close(Session *session) override;
  };

  pjs::Ref<Pool> m_pool;
  pjs::Ref<pjs::Function> m_session_selector;
  pjs::Ref<Queue> m_queue;
  pjs::Ref<Request> m_request;
  Muxer::Options m_options;
  bool m_has_error = false;

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
};

} // namespace fcgi
} // namespace pipy

#endif // FCGI_HPP
