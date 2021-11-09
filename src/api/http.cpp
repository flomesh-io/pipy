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
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACT// ION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "http.hpp"
#include "codebase.hpp"
#include "context.hpp"
#include "tar.hpp"
#include "utils.hpp"

#define ZLIB_CONST
#include <zlib.h>

namespace pipy {
namespace http {

enum StringConstants {
  CONTENT_TYPE,
  CONTENT_ENCODING,
  CONTENT_ENCODING_GZIP,
  CONTENT_ENCODING_BR,
};

static std::map<std::string, std::string> s_content_types = {
  { "html"  , "text/html" },
  { "css"   , "text/css" },
  { "xml"   , "text/xml" },
  { "txt"   , "text/plain" },
  { "gif"   , "image/gif" },
  { "png"   , "image/png" },
  { "jpg"   , "image/jpeg" },
  { "svg"   , "image/svg+xml" },
  { "woff"  , "font/woff" },
  { "woff2" , "font/woff2" },
  { "ico"   , "image/x-icon" },
  { "js"    , "application/javascript" },
  { "json"  , "application/json" },
};

static Data::Producer s_dp_http_file("http.File");

auto File::from(const std::string &path) -> File* {
  try {
    return File::make(path);
  } catch (std::runtime_error &err) {
    return nullptr;
  }
}

auto File::from(Tarball *tarball, const std::string &path) -> File* {
  try {
    return File::make(tarball, path);
  } catch (std::runtime_error &err) {
    return nullptr;
  }
}

File::File(const std::string &path) {
  load(path, [](const std::string &filename) -> Data* {
    return Codebase::current()->get(filename);
  });

  m_path = pjs::Str::make(path);
}

File::File(Tarball *tarball, const std::string &path) {
  auto filename = path;
  if (filename == "/") filename = "/index.html";

  load(filename, [=](const std::string &filename) -> Data* {
    size_t size;
    auto ptr = tarball->get(filename, size);
    if (!ptr) return nullptr;
    return s_dp_http_file.make(ptr, size);
  });

  m_path = pjs::Str::make(path);
}

void File::load(const std::string &filename, std::function<Data*(const std::string&)> get_file) {
  auto path = filename;
  pjs::Ref<Data> raw = get_file(path);
  pjs::Ref<Data> gz = get_file(path + ".gz");
  pjs::Ref<Data> br = get_file(path + ".br");

  if (!raw && !gz && !br) {
    if (path.back() != '/') path += '/';
    path += "index.html";
    raw = get_file(path);
    gz = get_file(path + ".gz");
    br = get_file(path + ".br");
    if (!raw && !gz && !br) {
      std::string msg("file not found: ");
      throw std::runtime_error(msg + filename);
    }
  }

  auto p = path.find_last_of('/');
  std::string name = (p == std::string::npos ? path : path.substr(p + 1));
  std::string ext;
  p = name.find_last_of('.');
  if (p != std::string::npos) ext = name.substr(p+1);

  auto k = ext;
  for (auto &c : k) c = std::tolower(c);

  auto i = s_content_types.find(k);
  auto ct = (i == s_content_types.end() ? "application/octet-stream" : i->second);

  m_name = pjs::Str::make(name);
  m_extension = pjs::Str::make(ext);
  m_content_type = pjs::Str::make(ct);
  m_data = raw;
  m_data_gz = gz;
  m_data_br = br;
}

auto File::to_message(pjs::Str *accept_encoding) -> pipy::Message* {
  auto &s = accept_encoding->str();
  bool has_gzip = false;
  bool has_deflate = false;
  bool has_br = false;
  size_t i = 0;
  for (size_t i = 0; i < s.length(); i++) {
    while (i < s.length() && std::isblank(s[i])) i++;
    if (i < s.length()) {
      auto n = 0; while (std::isalpha(s[i+n])) n++;
      if (n == 4 && !strncasecmp(&s[i], "gzip", n)) has_gzip = true;
      else if (n == 7 && !strncasecmp(&s[i], "deflate", n)) has_deflate = true;
      else if (n == 2 && !strncasecmp(&s[i], "br", n)) has_br = true;
      i += n;
      while (i < s.length() && s[i] != ',') i++;
    }
  }

  if (has_br && m_data_br) {
    if (!m_message_br) {
      auto head = ResponseHead::make();
      auto headers = Object::make();
      head->headers(headers);
      headers->set(
        pjs::EnumDef<StringConstants>::name(CONTENT_TYPE),
        m_content_type.get()
      );
      headers->set(
        pjs::EnumDef<StringConstants>::name(CONTENT_ENCODING),
        pjs::EnumDef<StringConstants>::name(CONTENT_ENCODING_BR)
      );
      m_message_br = Message::make(head, m_data_br);
    }
    return m_message_br;

  } else if (has_gzip && m_data_gz) {
    if (!m_message_gz) {
      auto head = ResponseHead::make();
      auto headers = Object::make();
      head->headers(headers);
      headers->set(
        pjs::EnumDef<StringConstants>::name(CONTENT_TYPE),
        m_content_type.get()
      );
      headers->set(
        pjs::EnumDef<StringConstants>::name(CONTENT_ENCODING),
        pjs::EnumDef<StringConstants>::name(CONTENT_ENCODING_GZIP)
      );
      m_message_gz = Message::make(head, m_data_gz);
    }
    return m_message_gz;

  } else {
    if (!m_message) {
      if (!m_data) decompress();
      if (!m_data) {
        auto head = ResponseHead::make();
        head->status(400);
        m_message = Message::make(head, nullptr);
      } else {
        auto head = ResponseHead::make();
        auto headers = Object::make();
        head->headers(headers);
        headers->set(pjs::EnumDef<StringConstants>::name(CONTENT_TYPE), m_content_type.get());
        m_message = Message::make(head, m_data);
      }
    }
    return m_message;
  }
}

bool File::decompress() {
  unsigned char buf[DATA_CHUNK_SIZE];

  if (m_data_gz) {
    m_data = Data::make();
    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.next_in = Z_NULL;
    zs.avail_in = 0;
    inflateInit2(&zs, 16 + MAX_WBITS);
    bool done = false;
    for (const auto &chk : m_data_gz->chunks()) {
      zs.next_in = (const unsigned char *)std::get<0>(chk);
      zs.avail_in = std::get<1>(chk);
      do {
        zs.next_out = buf;
        zs.avail_out = sizeof(buf);
        auto ret = inflate(&zs, Z_NO_FLUSH);
        if (auto size = sizeof(buf) - zs.avail_out) {
          s_dp_http_file.push(m_data, buf, size);
        }
        if (ret == Z_STREAM_END) { done = true; break; }
        if (ret != Z_OK) {
          inflateEnd(&zs);
          return false;
        }
      } while (zs.avail_out == 0);
      if (done) break;
    }
    inflateEnd(&zs);
    return true;

  } else if (m_data_br) {
  }

  return false;
}

} // namespace http
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::http;

template<> void EnumDef<StringConstants>::init() {
  define(CONTENT_TYPE, "content-type");
  define(CONTENT_ENCODING, "content-encoding");
  define(CONTENT_ENCODING_GZIP, "gzip");
  define(CONTENT_ENCODING_BR, "br");
}

template<> void ClassDef<MessageHead>::init() {
  variable("protocol", MessageHead::Field::protocol);
  variable("headers", MessageHead::Field::headers);
}

template<> void ClassDef<RequestHead>::init() {
  super<MessageHead>();
  ctor();
  variable("method", RequestHead::Field::method);
  variable("path", RequestHead::Field::path);
}

template<> void ClassDef<ResponseHead>::init() {
  super<MessageHead>();
  ctor();
  variable("status", ResponseHead::Field::status);
  variable("statusText", ResponseHead::Field::statusText);
}

template<> void ClassDef<File>::init() {
  ctor([](Context &ctx) -> Object* {
    std::string path;
    if (!ctx.arguments(1, &path)) return nullptr;
    try {
      return File::make(path);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("toMessage", [](Context &ctx, Object *obj, Value &ret) {
    Str *accept_encoding = Str::empty;
    if (!ctx.arguments(0, &accept_encoding)) return;
    ret.set(obj->as<File>()->to_message(accept_encoding));
  });
}

template<> void ClassDef<Constructor<File>>::init() {
  super<Function>();
  ctor();

  method("from", [](Context &ctx, Object *obj, Value &ret) {
    std::string path;
    if (!ctx.arguments(1, &path)) return;
    ret.set(File::from(path));
  });
}

template<> void ClassDef<Http>::init() {
  ctor();
  variable("File", class_of<Constructor<File>>());
}

} // namespace pjs
