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

#include "module.hpp"
#include "buffer.hpp"

NS_BEGIN

namespace http {

  //
  // RequestDecoder
  //

  class RequestDecoder : public Module {
  public:
    RequestDecoder();

  private:
    ~RequestDecoder();

    virtual auto help() -> std::list<std::string> override;
    virtual void config(const std::map<std::string, std::string> &params) override;
    virtual auto clone() -> Module* override;
    virtual void pipe(
      std::shared_ptr<Context> ctx,
      std::unique_ptr<Object> obj,
      Object::Receiver receiver
    ) override;

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

    std::string m_var_protocol;
    std::string m_var_method;
    std::string m_var_path;
    std::string m_var_headers;

    State m_state;
    CharBuf<0x10000> m_name;
    CharBuf<0x10000> m_value;
    std::string m_transfer_encoding;
    bool m_chunked = false;
    int m_content_length;
    int m_message_count = 0;
  };

  //
  // ResponseDecoder
  //

  class ResponseDecoder : public Module {
  public:
    ResponseDecoder();

  private:
    ~ResponseDecoder();

    virtual auto help() -> std::list<std::string> override;
    virtual void config(const std::map<std::string, std::string> &params) override;
    virtual auto clone() -> Module* override;
    virtual void pipe(
      std::shared_ptr<Context> ctx,
      std::unique_ptr<Object> obj,
      Object::Receiver receiver
    ) override;

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

    std::string m_var_protocol;
    std::string m_var_status_code;
    std::string m_var_status;
    std::string m_var_headers;

    State m_state;
    CharBuf<0x10000> m_name;
    CharBuf<0x10000> m_value;
    std::string m_transfer_encoding;
    bool m_chunked;
    int m_content_length;
    int m_message_count = 0;
  };

  //
  // RequestEncoder
  //

  class RequestEncoder : public Module {
  public:
    RequestEncoder();

  private:
    ~RequestEncoder();

    virtual auto help() -> std::list<std::string> override;
    virtual void config(const std::map<std::string, std::string> &params) override;
    virtual auto clone() -> Module* override;
    virtual void pipe(
      std::shared_ptr<Context> ctx,
      std::unique_ptr<Object> obj,
      Object::Receiver receiver
    ) override;

    std::string m_var_protocol;
    std::string m_var_method;
    std::string m_var_path;
    std::string m_var_headers;
    std::string m_protocol;
    std::string m_method;
    std::string m_path;
    std::map<std::string, std::string> m_headers;
    std::unique_ptr<Data> m_buffer;

    static std::string s_default_protocol;
    static std::string s_default_method;
    static std::string s_default_path;
    static std::string s_header_content_length;
  };

  //
  // ResponseEncoder
  //

  class ResponseEncoder : public Module {
  public:
    ResponseEncoder();

  private:
    ~ResponseEncoder();

    virtual auto help() -> std::list<std::string> override;
    virtual void config(const std::map<std::string, std::string> &params) override;
    virtual auto clone() -> Module* override;
    virtual void pipe(
      std::shared_ptr<Context> ctx,
      std::unique_ptr<Object> obj,
      Object::Receiver receiver
    ) override;

    std::string m_var_method;
    std::string m_var_protocol;
    std::string m_var_status_code;
    std::string m_var_status;
    std::string m_var_headers;
    std::string m_var_connection;
    std::string m_var_keep_alive;
    std::string m_protocol;
    std::string m_status_code;
    std::string m_status;
    std::map<std::string, std::string> m_headers;
    std::unique_ptr<Data> m_buffer;

    static std::string s_default_protocol;
    static std::string s_default_status;
    static std::string s_default_status_code;
    static std::string s_header_content_length;
  };

} // namespace http

NS_END

#endif // HTTP_HPP
