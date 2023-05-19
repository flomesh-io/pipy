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

#ifndef API_HTTP_HPP
#define API_HTTP_HPP

#include "pjs/pjs.hpp"
#include "message.hpp"

#include <string>

namespace pipy {

class Tarball;

namespace http {

enum class TunnelType {
  NONE,
  CONNECT,
  WEBSOCKET,
  HTTP2,
};

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  pjs::Ref<pjs::Str> protocol;
  pjs::Ref<pjs::Object> headers;
};

class MessageTail : public pjs::ObjectTemplate<MessageTail> {
public:
  pjs::Ref<pjs::Object> headers;
};

class RequestHead : public pjs::ObjectTemplate<RequestHead, MessageHead> {
public:
  pjs::Ref<pjs::Str> method;
  pjs::Ref<pjs::Str> scheme;
  pjs::Ref<pjs::Str> authority;
  pjs::Ref<pjs::Str> path;

  bool is_final() const;
  bool is_final(pjs::Str *header_connection) const;
  auto tunnel_type() const -> TunnelType;
  auto tunnel_type(pjs::Str *header_upgrade) const -> TunnelType;
};

class ResponseHead : public pjs::ObjectTemplate<ResponseHead, MessageHead> {
public:
  int status = 200;
  pjs::Ref<pjs::Str> statusText;

  bool is_tunnel(TunnelType requested);

  static auto error_to_status(StreamEnd::Error err, int &status) -> pjs::Str*;
};

//
// File
//

class File : public pjs::ObjectTemplate<File> {
public:
  static auto from(const std::string &path) -> File*;
  static auto from(Tarball *tarball, const std::string &path) -> File*;

  auto to_message(pjs::Str *accept_encoding) -> Message*;

private:
  File(const std::string &path);
  File(Tarball *tarball, const std::string &path);

  pjs::Ref<pjs::Str> m_path;
  pjs::Ref<pjs::Str> m_name;
  pjs::Ref<pjs::Str> m_extension;
  pjs::Ref<pjs::Str> m_content_type;
  pjs::Ref<Data> m_data;
  pjs::Ref<Data> m_data_gz;
  pjs::Ref<Data> m_data_br;
  pjs::Ref<Message> m_message;
  pjs::Ref<Message> m_message_gz;
  pjs::Ref<Message> m_message_br;

  void load(const std::string &filename, std::function<Data*(const std::string&)> get_file);
  bool decompress();

  friend class pjs::ObjectTemplate<File>;
};

//
// Http
//

class Http : public pjs::ObjectTemplate<Http>
{
};

} // namespace http
} // namespace pipy

#endif // API_HTTP_HPP
