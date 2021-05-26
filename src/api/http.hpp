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

#ifndef FILTER_HTTP_HPP
#define FILTER_HTTP_HPP

#include "pjs/pjs.hpp"
#include "message.hpp"

#include <string>

namespace pipy {

class Tarball;

namespace http {

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  enum class Field {
    headers,
  };

  auto headers() -> Object* {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::headers, ret);
    return ret.is_object() ? ret.o() : nullptr;
  }

  void headers(pjs::Object *o) { pjs::set<MessageHead>(this, MessageHead::Field::headers, o); }
};

class RequestHead : public pjs::ObjectTemplate<RequestHead, MessageHead> {
public:
  enum class Field {
    protocol,
    method,
    path,
  };

  auto protocol() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<RequestHead>(this, RequestHead::Field::protocol, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  auto method() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<RequestHead>(this, RequestHead::Field::method, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  auto path() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<RequestHead>(this, RequestHead::Field::path, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  void protocol(pjs::Str *s) { pjs::set<RequestHead>(this, RequestHead::Field::protocol, s); }
  void method(pjs::Str *s) { pjs::set<RequestHead>(this, RequestHead::Field::method, s); }
  void path(pjs::Str *s) { pjs::set<RequestHead>(this, RequestHead::Field::path, s); }
};

class ResponseHead : public pjs::ObjectTemplate<ResponseHead, MessageHead> {
public:
  enum class Field {
    protocol,
    status,
    statusText,
  };

  auto protocol() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<ResponseHead>(this, ResponseHead::Field::protocol, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  auto status() -> int {
    pjs::Value ret;
    pjs::get<ResponseHead>(this, ResponseHead::Field::status, ret);
    return ret.is_number() ? ret.n() : 0;
  }

  auto status_text() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<ResponseHead>(this, ResponseHead::Field::statusText, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  void protocol(pjs::Str *s) { pjs::set<ResponseHead>(this, ResponseHead::Field::protocol, s); }
  void status(int n) { pjs::set<ResponseHead>(this, ResponseHead::Field::status, n); }
  void status_text(pjs::Str *s) { pjs::set<ResponseHead>(this, ResponseHead::Field::statusText, s); }
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

#endif // FILTER_HTTP_HPP