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
#include "buffer.hpp"
#include "data.hpp"
#include "session.hpp"
#include "api/http.hpp"

#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace pipy {

namespace http {

//
// Decoder
//

class Decoder {
public:
  Decoder(bool is_response, const Event::Receiver &output)
    : m_output(output)
    , m_is_response(is_response) {}

  void reset();

  void input(
    const pjs::Ref<Data> &data,
    const std::function<void(MessageHead*)> &on_message_start = nullptr
  );

  void end(SessionEnd *end);

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

  Event::Receiver m_output;
  State m_state = HEAD;
  Data m_head_buffer;
  pjs::Ref<MessageHead> m_head;
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_final = false;

  void output_start(const std::function<void(MessageHead*)> &on_message_start) {
    if (on_message_start) {
      on_message_start(m_head);
    }
    m_output(MessageStart::make(m_head));
  }

  void output_end() {
    m_output(MessageEnd::make());
    m_is_bodiless = false;
    m_is_final = false;
  }

  bool is_bodiless_response() const {
    return m_is_response && m_is_bodiless;
  }
};

//
// Encoder
//

class Encoder {
public:
  Encoder(bool is_response, const Event::Receiver &output)
    : m_output(output)
    , m_is_response(is_response) {}

  void reset();

  void input(const pjs::Ref<Event> &evt);

  bool is_bodiless() const { return m_is_bodiless; }

  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_final(bool b) { m_is_final = b; }

private:
  Event::Receiver m_output;
  pjs::Ref<MessageStart> m_start;
  pjs::Ref<Data> m_buffer;
  int m_content_length = -1;
  bool m_chunked = false;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_final = false;

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

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  Decoder m_decoder;
  bool m_session_end = false;
};

//
// ResponseDecoder
//

class ResponseDecoder : public Filter {
public:
  ResponseDecoder();
  ResponseDecoder(pjs::Object *options);
  ResponseDecoder(const std::function<bool()> &bodiless);

private:
  ResponseDecoder(const ResponseDecoder &r);
  ~ResponseDecoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  Decoder m_decoder;
  std::function<bool()> m_bodiless_func;
  pjs::Value m_bodiless;
  bool m_session_end = false;

  bool is_bodiless(Context *ctx);
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

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  Encoder m_encoder;
  bool m_session_end = false;
};

//
// ResponseEncoder
//

class ResponseEncoder : public Filter {
public:
  ResponseEncoder();
  ResponseEncoder(pjs::Object *options);

private:
  ResponseEncoder(const ResponseEncoder &r);
  ~ResponseEncoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  Encoder m_encoder;
  pjs::Value m_bodiless;
  bool m_session_end = false;
};

//
// Demux
//

class ServerConnection;

class Demux : public Filter {
public:
  Demux();
  Demux(Pipeline *pipeline);
  Demux(pjs::Str *target);

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  Pipeline* m_pipeline = nullptr;
  pjs::Ref<pjs::Str> m_target;
  ServerConnection* m_connection = nullptr;
  bool m_session_end = false;
};

//
// Mux
//

class Mux : public MuxBase {
public:
  Mux();
  Mux(Pipeline *pipeline, const pjs::Value &channel);
  Mux(pjs::Str *target, const pjs::Value &channel);

private:
  Mux(const Mux &r);
  ~Mux();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual auto clone() -> Filter* override;
  virtual auto new_connection() -> Connection* override;
};

//
// Server
//

class Server : public Filter {
public:
  Server();
  Server(const std::function<Message*(Context*, Message*)> &handler);
  Server(pjs::Object *handler);

private:
  Server(const Server &r);
  ~Server();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  struct Request {
    bool is_bodiless;
    bool is_final;
  };

  std::function<Message*(Context*, Message*)> m_handler_func;
  pjs::Ref<pjs::Object> m_handler;
  Decoder m_decoder;
  Encoder m_encoder;
  Context* m_context = nullptr;
  pjs::Ref<MessageStart> m_head;
  pjs::Ref<Data> m_body;
  std::list<Request> m_queue;
  bool m_session_end = false;

  void request(const pjs::Ref<Event> &evt);
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP