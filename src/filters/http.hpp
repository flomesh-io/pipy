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

#include "filter.hpp"
#include "buffer.hpp"
#include "data.hpp"
#include "session.hpp"

namespace pipy {

class Decompressor;

namespace http {

class RequestHead;
class ResponseHead;

//
// RequestDecoder
//

class RequestDecoder : public Filter {
public:
  RequestDecoder();
  RequestDecoder(pjs::Object *options);

private:
  RequestDecoder(const RequestDecoder &r);
  ~RequestDecoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  enum State {
    METHOD,
    PATH,
    PROTOCOL,
    HEADER_NAME,
    HEADER_VALUE,
    BODY,
    CHUNK_HEAD,
    CHUNK_TAIL,
    CHUNK_TAIL_LAST,
  };

  State m_state;
  CharBuf<0x10000> m_name;
  CharBuf<0x10000> m_value;
  pjs::Ref<RequestHead> m_head;
  std::string m_protocol;
  std::string m_content_encoding;
  std::string m_transfer_encoding;
  std::string m_connection;
  std::string m_keep_alive;
  pjs::Value m_decompress;
  Decompressor* m_decompressor = nullptr;
  bool m_session_end = false;
  bool m_chunked = false;
  bool m_connected = false;
  int m_content_length;

  bool is_keep_alive();
  void end_message(Context *ctx);
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

  enum State {
    PROTOCOL,
    STATUS_CODE,
    STATUS,
    HEADER_NAME,
    HEADER_VALUE,
    BODY,
    CHUNK_HEAD,
    CHUNK_TAIL,
    CHUNK_TAIL_LAST,
  };

  State m_state;
  CharBuf<0x10000> m_name;
  CharBuf<0x10000> m_value;
  pjs::Ref<ResponseHead> m_head;
  std::string m_content_encoding;
  std::string m_transfer_encoding;
  std::function<bool()> m_bodiless_func;
  pjs::Value m_bodiless;
  pjs::Value m_decompress;
  Decompressor* m_decompressor = nullptr;
  bool m_session_end = false;
  bool m_chunked;
  int m_content_length;

  bool is_bodiless(Context *ctx);
};

//
// RequestEncoder
//

class RequestEncoder : public Filter {
public:
  RequestEncoder();
  RequestEncoder(pjs::Object *head);

private:
  RequestEncoder(const RequestEncoder &r);
  ~RequestEncoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  pjs::Ref<MessageStart> m_message_start;
  pjs::Ref<pjs::Object> m_head;
  pjs::Ref<Data> m_buffer;
  pjs::PropertyCache m_prop_protocol;
  pjs::PropertyCache m_prop_method;
  pjs::PropertyCache m_prop_path;
  pjs::PropertyCache m_prop_headers;
  bool m_session_end = false;

  static std::string s_default_protocol;
  static std::string s_default_method;
  static std::string s_default_path;
  static std::string s_header_content_length;
};

//
// ResponseEncoder
//

class ResponseEncoder : public Filter {
public:
  ResponseEncoder();
  ResponseEncoder(pjs::Object *head);

private:
  ResponseEncoder(const ResponseEncoder &r);
  ~ResponseEncoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  pjs::Ref<MessageStart> m_message_start;
  pjs::Ref<pjs::Object> m_head;
  pjs::Ref<Data> m_buffer;
  pjs::PropertyCache m_prop_protocol;
  pjs::PropertyCache m_prop_status;
  pjs::PropertyCache m_prop_status_text;
  pjs::PropertyCache m_prop_headers;
  pjs::PropertyCache m_prop_bodiless;
  bool m_session_end = false;

  static std::string s_default_protocol;
  static std::string s_default_status;
  static std::string s_default_status_text;
  static std::string s_header_connection_keep_alive;
  static std::string s_header_connection_close;
  static std::string s_header_content_length;
};

//
// Server
//

class Server : public Filter {
public:
  Server();
  Server(pjs::Str *target);

  class Handler {
  public:
    virtual void input(Data *data) = 0;
    virtual void close() = 0;
  };

private:
  Server(const Server &r);
  ~Server();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  pjs::Ref<pjs::Str> m_target;
  Handler* m_handler = nullptr;
  bool m_session_end = false;
};

} // namespace http
} // namespace pipy

#endif // HTTP_HPP