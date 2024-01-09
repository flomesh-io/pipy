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
#include "filters/connect.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "codebase.hpp"
#include "fs.hpp"
#include "compressor.hpp"
#include "utils.hpp"

namespace pipy {
namespace http {

//
// RequestHead
//

thread_local static const pjs::ConstStr s_HEAD("HEAD");
thread_local static const pjs::ConstStr s_CONNECT("CONNECT");
thread_local static const pjs::ConstStr s_connection("connection");
thread_local static const pjs::ConstStr s_upgrade("upgrade");
thread_local static const pjs::ConstStr s_close("close");
thread_local static const pjs::ConstStr s_http_1_0("HTTP/1.0");
thread_local static const pjs::ConstStr s_websocket("websocket");
thread_local static const pjs::ConstStr s_h2c("h2c");
thread_local static const pjs::ConstStr s_host("host");
thread_local static const pjs::ConstStr s_Host("Host");
thread_local static const pjs::ConstStr s_bad_gateway("Bad Gateway");
thread_local static const pjs::ConstStr s_cannot_resolve("Cannot Resolve");
thread_local static const pjs::ConstStr s_connection_refused("Connection Refused");
thread_local static const pjs::ConstStr s_unauthorized("Unauthorized");
thread_local static const pjs::ConstStr s_read_error("Read Error");
thread_local static const pjs::ConstStr s_write_error("Write Error");
thread_local static const pjs::ConstStr s_gateway_timeout("Gateway Timeout");
thread_local static const pjs::ConstStr s_accept_encoding("accept-encoding");
thread_local static const pjs::ConstStr s_content_encoding("content-encoding");
thread_local static const pjs::ConstStr s_content_type("content-type");
thread_local static const pjs::ConstStr s_application_octet_stream("application/octet-stream");
thread_local static const pjs::ConstStr s_gzip("gzip");
thread_local static const pjs::ConstStr s_br("br");

static const std::map<std::string, std::string> s_default_content_types = {
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

bool MessageHead::is_final() const {
  pjs::Value v;
  if (headers && headers->get(s_connection, v)) {
    return v.is_string() && v.s() == s_close;
  } else {
    return protocol == s_http_1_0;
  }
}

bool MessageHead::is_final(pjs::Str *header_connection) const {
  if (header_connection) {
    return header_connection == s_close;
  } else {
    return protocol == s_http_1_0;
  }
}

auto RequestHead::tunnel_type() const -> TunnelType {
  if (method == s_CONNECT) return TunnelType::CONNECT;
  pjs::Value v;
  if (headers && headers->get(s_upgrade, v) && v.is_string()) {
    if (v.s() == s_websocket) return TunnelType::WEBSOCKET;
    if (v.s() == s_h2c) return TunnelType::HTTP2;
  }
  return TunnelType::NONE;
}

auto RequestHead::tunnel_type(pjs::Str *header_upgrade) const -> TunnelType {
  if (method == s_CONNECT) return TunnelType::CONNECT;
  if (header_upgrade) {
    if (header_upgrade == s_websocket) return TunnelType::WEBSOCKET;
    if (header_upgrade == s_h2c) return TunnelType::HTTP2;
  }
  return TunnelType::NONE;
}

//
// ResponseHead
//

bool ResponseHead::is_tunnel_ok(TunnelType requested) {
  switch (requested) {
    case TunnelType::NONE: break;
    case TunnelType::CONNECT: return (200 <= status && status < 300);
    case TunnelType::WEBSOCKET: return (status == 101);
    case TunnelType::HTTP2: return (status == 101);
  }
  return false;
}

auto ResponseHead::error_to_status(StreamEnd::Error err, int &status) -> pjs::Str* {
  switch (err) {
  case StreamEnd::CANNOT_RESOLVE:
    status = 502;
    return s_cannot_resolve;
  case StreamEnd::CONNECTION_REFUSED:
    status = 502;
    return s_connection_refused;
  case StreamEnd::UNAUTHORIZED:
    status = 401;
    return s_unauthorized;
  case StreamEnd::READ_ERROR:
    status = 502;
    return s_read_error;
  case StreamEnd::WRITE_ERROR:
    status = 502;
    return s_write_error;
  case StreamEnd::CONNECTION_TIMEOUT:
  case StreamEnd::READ_TIMEOUT:
  case StreamEnd::WRITE_TIMEOUT:
    status = 504;
    return s_gateway_timeout;
  default:
    status = 502;
    return s_bad_gateway;
  }
}

//
// Agent
//

thread_local Data::Producer Agent::s_dp("http.Agent");

Agent::Agent(pjs::Str *host, pjs::Object *options)
  : m_module(new Module)
  , m_host(host)
{
  pjs::Ref<pjs::Object> tls;
  pipy::Options::Value(options, "tls").get(tls).check_nullable();

  std::string target, addr;
  uint16_t ip[8];
  int port;

  if (utils::get_ip_v6(host->str(), ip)) {
    target = std::string("[") + host->str() + (tls ? "]:443" : "]:80");
  } else if (utils::get_host_port(host->str(), addr, port)) {
    target = host->str();
  } else {
    target = host->str() + (tls ? ":443" : ":80");
  }

  auto pl_connect = PipelineLayout::make(m_module);
  pl_connect->append(new Connect(target, options));

  if (tls) {
    auto pl_tls = PipelineLayout::make(m_module);
    pl_tls->append(new tls::Client(tls.get()))->add_sub_pipeline(pl_connect);
    pl_connect = pl_tls;
  }

  m_pipeline_layout = PipelineLayout::make(m_module);
  m_pipeline_layout->append(new http::Mux(nullptr, options))->add_sub_pipeline(pl_connect);
}

auto Agent::request(Message *req) -> pjs::Promise* {
  pjs::Ref<RequestHead> head(pjs::coerce<RequestHead>(req->head()));
  return request(head->method, head->path, head->headers, req->body());
}

auto Agent::request(pjs::Str *method, pjs::Str *path, pjs::Object *headers, Data *body) -> pjs::Promise* {
  if (!headers || (!headers->ht_has(s_host) && headers->ht_has(s_Host))) {
    if (headers) {
      auto *new_headers = pjs::Object::make();
      pjs::Object::assign(new_headers, headers);
      headers = new_headers;
    } else {
      headers = pjs::Object::make();
    }
    headers->set(s_host, m_host.get());
  }

  auto head = RequestHead::make();
  head->method = method;
  head->path = path;
  head->headers = headers;

  auto r = new Request(this);
  return r->start(head, body);
}

auto Agent::request(pjs::Str *method, pjs::Str *path, pjs::Object *headers, pjs::Str *body) -> pjs::Promise* {
  return request(method, path, headers, Data::make(body->str(), &s_dp));
}

//
// Agent::Request
//

auto Agent::Request::start(RequestHead *head, Data *body) -> pjs::Promise* {
  auto pl = m_agent->m_pipeline_layout.get();
  auto p = Pipeline::make(pl, pl->new_context());
  p->chain(EventSource::reply());
  m_pipeline = p;

  auto promise = pjs::Promise::make();
  m_settler = pjs::Promise::Settler::make(promise);

  if (InputContext::origin()) {
    send(head, body);
  } else {
    InputContext ic;
    send(head, body);
  }

  return promise;
}

void Agent::Request::send(RequestHead *head, Data *body) {
  Pipeline::auto_release(m_pipeline);
  auto i = m_pipeline->input();
  i->input(MessageStart::make(head));
  if (body) i->input(body);
  i->input(MessageEnd::make());
}

void Agent::Request::on_reply(Event *evt) {
  Pipeline::auto_release(m_pipeline);
  if (auto msg = m_message_reader.read(evt)) {
    m_settler->resolve(msg);
    EventSource::close();
    delete this;
  }
}

//
// Directory::Options
//

Directory::Options::Options(pjs::Object *options) {
  Value(options, "fs")
    .get(fs)
    .check_nullable();
  Value(options, "tarball")
    .get(tarball)
    .check_nullable();
  Value(options, "index")
    .get(index)
    .get(index_list)
    .check_nullable();
  Value(options, "contentTypes")
    .get(content_types_f)
    .get(content_types)
    .check_nullable();
  Value(options, "defaultContentType")
    .get(default_content_type)
    .check_nullable();
  Value(options, "compression")
    .get(compression_f)
    .check_nullable();
}

//
// Directory
//

thread_local Data::Producer Directory::s_dp("http::Directory");

Directory::Directory(const std::string &path)
  : Directory(path, Options()) {}

Directory::Directory(const std::string &path, const Options &options)
  : m_options(options)
{
  if (options.tarball) {
    std::vector<uint8_t> data;
    if (options.fs) {
      fs::read_file(path, data);
    } else if (auto codebase = Codebase::current()) {
      if (auto sd = codebase->get(path)) {
        Data buf;
        sd->to_data(buf);
        sd->release();
        data = buf.to_bytes();
      }
    }
    if (data.size() > 0) {
      m_loader = new TarballLoader((const char *)&data[0], data.size());
    }
  } else if (options.fs) {
    m_loader = new FileSystemLoader(path);
  } else {
    m_loader = new CodebaseLoader(path);
  }

  if (auto a = options.index_list.get()) {
    a->iterate_all(
      [this](pjs::Value &v, int) {
        auto s = v.to_string();
        m_index_filenames.push_back(s->str());
        s->release();
      }
    );
  } else if (auto s = options.index.get()) {
    m_index_filenames.push_back(s->str());
  } else {
    m_index_filenames.push_back("index");
    m_index_filenames.push_back("index.html");
  }

  if (auto o = options.content_types.get()) {
    set_content_types(o);
  } else {
    for (const auto &p : s_default_content_types) {
      m_content_types[p.first] = pjs::Str::make(p.second);
    }
  }

  if (auto s = options.default_content_type.get()) {
    m_default_content_type = s;
  } else {
    m_default_content_type = s_application_octet_stream;
  }
}

Directory::~Directory() {
  delete m_loader;
}

auto Directory::serve(pjs::Context &ctx, Message *request) -> Message* {
  if (!m_loader) return nullptr;

  pjs::Ref<RequestHead> head = pjs::coerce<RequestHead>(request->head());
  auto path = head->path ? utils::path_normalize(head->path->str()) : std::string();
  auto n = path.find('?');
  if (n != std::string::npos) path = path.substr(0, n);

  auto k = path;
  auto i = m_cache.find(k);
  if (i == m_cache.end()) {
    Data raw, gz, br;
    if (!m_loader->load_file(path, raw)) {
      if (path.empty() || path.back() != '/') path += '/';
      bool found = false;
      for (const auto &s : m_index_filenames) {
        auto index_path = path + s;
        if (m_loader->load_file(index_path, raw)) {
          path = index_path;
          found = true;
          break;
        }
      }
      if (!found) return nullptr;
    }
    m_loader->load_file(path + ".gz", gz);
    m_loader->load_file(path + ".br", br);

    auto &f = m_cache[k];
    f.pathname = pjs::Str::make(path);
    f.raw = std::move(raw);
    f.gz = std::move(gz);
    f.br = std::move(br);

    std::string ext;
    auto p = path.find('.', path.rfind('/'));
    if (p != std::string::npos) ext = path.substr(p+1);
    for (auto &c : ext) c = std::tolower(c);

    if (auto *cb = m_options.content_types_f.get()) {
      pjs::Value arg[2], ret;
      arg[0].set(request);
      arg[1].set(f.pathname);
      (*cb)(ctx, 2, arg, ret);
      if (!ctx.ok()) return nullptr;
      if (ret.is_object()) {
        pjs::Value ct;
        ret.o()->get(ext, ct);
        auto s = ct.to_string();
        f.content_type = s;
        s->release();
      } else if (!ret.is_nullish()) {
        auto s = ret.to_string();
        f.content_type = s;
        s->release();
      }
    }

    if (!f.content_type) {
      auto i = m_content_types.find(ext);
      f.content_type = i == m_content_types.end() ? m_default_content_type.get() : i->second.get();
    }

    return get_encoded_response(ctx, f, head);
  }

  return get_encoded_response(ctx, i->second, head);
}

void Directory::set_content_types(pjs::Object *obj) {
  m_content_types.clear();
  if (obj) {
    obj->iterate_all(
      [this](pjs::Str *k, pjs::Value v) {
        auto s = v.to_string();
        m_content_types[k->str()] = s;
        s->release();
      }
    );
  }
}

auto Directory::get_encoded_response(pjs::Context &ctx, File &file, RequestHead *request) -> Message* {
  bool has_gz = false;
  bool has_br = false;

  pjs::Value accept_encoding;
  if (auto headers = request->headers.get()) {
    headers->get(s_accept_encoding.get(), accept_encoding);
  }
  if (accept_encoding.is_string()) {
    auto &s = accept_encoding.s()->str();
    for (size_t i = 0; i < s.length(); i++) {
      while (i < s.length() && std::isblank(s[i])) i++;
      if (i < s.length()) {
        auto n = 0; while (std::isalpha(s[i+n])) n++;
        if (n == 4 && utils::iequals(&s[i], "gzip", n)) has_gz = true;
        else if (n == 2 && utils::iequals(&s[i], "br", n)) has_br = true;
        i += n;
        while (i < s.length() && s[i] != ',') i++;
      }
    }
  }

  Message *response = nullptr;
  auto head = ResponseHead::make();
  auto headers = Object::make();
  head->headers = headers;
  headers->set(s_content_type.get(), file.content_type.get());

  if (has_br && !file.br.empty()) {
    headers->set(s_content_encoding.get(), s_br.get());
    response = Message::make(head, Data::make(file.br));

  } else if (has_gz && !file.gz.empty()) {
    headers->set(s_content_encoding.get(), s_gzip.get());
    response = Message::make(head, Data::make(file.gz));

  } else {
    Data *body = nullptr;
    Compressor *compressor = nullptr;
    auto output = [&](const Data &data) { body->push(data); };

    if ((has_gz || has_br) && m_options.compression_f) {
      auto accept_encoding = pjs::Object::make();
      if (has_gz) accept_encoding->set(s_gzip, true);
      if (has_br) accept_encoding->set(s_br, true);
      pjs::Value args[4], ret;
      args[0].set(request);
      args[1].set(accept_encoding);
      args[2].set(file.pathname.get());
      args[3].set(file.raw.size());
      (*m_options.compression_f)(ctx, 4, args, ret);
      if (!ctx.ok()) return nullptr;
      if (ret.to_boolean()) {
        if (!ret.is_string()) {
          ctx.error("callback expected to return a string");
          return nullptr;
        }
        if (ret.s() == s_gzip) {
          compressor = Compressor::gzip(output);
          body = &file.gz;
          headers->set(s_content_encoding.get(), s_gzip.get());
        } else {
          ctx.error("callback returned an unsupported compression algorithm");
          return nullptr;
        }
      }
    }

    if (compressor) {
      compressor->input(file.raw, true);
      compressor->finalize();
      response = Message::make(head, Data::make(*body));
    } else {
      response = Message::make(head, Data::make(file.raw));
    }
  }

  return response;
}

//
// Directory::CodebaseLoader
//

Directory::CodebaseLoader::CodebaseLoader(const std::string &path)
  : m_root_path(path)
{
}

bool Directory::CodebaseLoader::load_file(const std::string &path, Data &data) {
  if (auto codebase = Codebase::current()) {
    if (auto sd = codebase->get(utils::path_join(m_root_path, path))) {
      sd->to_data(data);
      sd->release();
      return true;
    }
  }
  return false;
}

//
// FileSystemLoader
//

Directory::FileSystemLoader::FileSystemLoader(const std::string &path)
  : m_root_path(path)
{
}

bool Directory::FileSystemLoader::load_file(const std::string &path, Data &data) {
  std::vector<uint8_t> buf;
  auto full_path = utils::path_join(m_root_path, path);
  if (fs::is_file(full_path)) {
    if (fs::read_file(full_path, buf)) {
      data.push(&buf[0], buf.size(), &s_dp);
      return true;
    }
  }
  return false;
}

//
// TarballLoader
//

Directory::TarballLoader::TarballLoader(const char *data, size_t size)
  : m_tarball(data, size)
{
}

bool Directory::TarballLoader::load_file(const std::string &path, Data &data) {
  size_t size;
  if (auto ptr = m_tarball.get(path, size)) {
    data.push(ptr, size, &s_dp);
    return true;
  }
  return false;
}

//
// File
//

enum StringConstants {
  CONTENT_TYPE,
  CONTENT_ENCODING,
  CONTENT_ENCODING_GZIP,
  CONTENT_ENCODING_BR,
};

thread_local static Data::Producer s_dp_http_file("http.File");

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
    auto sd = Codebase::current()->get(filename);
    if (!sd) return nullptr;
    auto data = Data::make(*sd);
    sd->release();
    return data;
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

  auto i = s_default_content_types.find(k);
  auto ct = (i == s_default_content_types.end() ? "application/octet-stream" : i->second);

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
  bool has_br = false;
  for (size_t i = 0; i < s.length(); i++) {
    while (i < s.length() && std::isblank(s[i])) i++;
    if (i < s.length()) {
      auto n = 0; while (std::isalpha(s[i+n])) n++;
      if (n == 4 && utils::iequals(&s[i], "gzip", n)) has_gzip = true;
      else if (n == 2 && utils::iequals(&s[i], "br", n)) has_br = true;
      i += n;
      while (i < s.length() && s[i] != ',') i++;
    }
  }

  if (has_br && m_data_br) {
    if (!m_message_br) {
      auto head = ResponseHead::make();
      auto headers = Object::make();
      head->headers = headers;
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
      head->headers = headers;
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
        head->status = 400;
        m_message = Message::make(head, nullptr);
      } else {
        auto head = ResponseHead::make();
        auto headers = Object::make();
        head->headers = headers;
        headers->set(pjs::EnumDef<StringConstants>::name(CONTENT_TYPE), m_content_type.get());
        m_message = Message::make(head, m_data);
      }
    }
    return m_message;
  }
}

bool File::decompress() {
  Decompressor *decomp;
  bool result = false;
  if(m_data_gz || m_data_br) {
    m_data = Data::make();
    auto func = [this](Data &data) {
      m_data->push(std::move(data));
    };
    decomp = m_data_gz ? Decompressor::inflate(func) : Decompressor::brotli(func);
    result = decomp->input(m_data_gz ? *m_data_gz : *m_data_br);
    decomp->finalize();
  }
  return result;
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
  field<pjs::Ref<pjs::Str>>("protocol", [](MessageHead *obj) { return &obj->protocol; });
  field<pjs::Ref<pjs::Object>>("headers", [](MessageHead *obj) { return &obj->headers; });
}

template<> void ClassDef<MessageTail>::init() {
  field<pjs::Ref<pjs::Object>>("headers", [](MessageTail *obj) { return &obj->headers; });
  field<int>("headSize", [](MessageTail *obj) { return &obj->headSize; });
  field<int>("bodySize", [](MessageTail *obj) { return &obj->bodySize; });
}

template<> void ClassDef<RequestHead>::init() {
  super<MessageHead>();
  ctor();
  field<pjs::Ref<pjs::Str>>("method", [](RequestHead *obj) { return &obj->method; });
  field<pjs::Ref<pjs::Str>>("scheme", [](RequestHead *obj) { return &obj->scheme; });
  field<pjs::Ref<pjs::Str>>("authority", [](RequestHead *obj) { return &obj->authority; });
  field<pjs::Ref<pjs::Str>>("path", [](RequestHead *obj) { return &obj->path; });
}

template<> void ClassDef<ResponseHead>::init() {
  super<MessageHead>();
  ctor();
  field<int>("status", [](ResponseHead *obj) { return &obj->status; });
  field<pjs::Ref<pjs::Str>>("statusText", [](ResponseHead *obj) { return &obj->statusText; });
}

template<> void ClassDef<Agent>::init() {
  ctor([](Context &ctx) -> Object* {
    pjs::Str *target;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return nullptr;
    try {
      return Agent::make(target, options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("request", [](Context &ctx, Object *obj, Value &ret) {
    Message *req;
    Str *method, *path, *body_str;
    Object *headers = nullptr;
    pipy::Data *body;
    if (ctx.try_arguments(1, &req) && req) {
      ret.set(static_cast<Agent*>(obj)->request(req));
    } else if (ctx.arguments(2, &method, &path, &headers)) {
      if (ctx.is_undefined(3)) {
        ret.set(static_cast<Agent*>(obj)->request(method, path, headers));
      } else if (ctx.get(3, body_str)) {
        ret.set(static_cast<Agent*>(obj)->request(method, path, headers, body_str));
      } else if (ctx.get(3, body)) {
        ret.set(static_cast<Agent*>(obj)->request(method, path, headers, body));
      } else {
        ctx.error_argument_type(3, "a string or a Data object");
      }
    }
  });
}

template<> void ClassDef<Constructor<Agent>>::init() {
  super<Function>();
  ctor();
}

template<> void ClassDef<Directory>::init() {
  ctor([](Context &ctx) -> Object* {
    pjs::Str *path;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &path, &options)) return nullptr;
    try {
      return Directory::make(path->str(), options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("serve", [](Context &ctx, Object *obj, Value &ret) {
    Message *request;
    if (!ctx.arguments(1, &request)) return;
    ret.set(obj->as<Directory>()->serve(ctx, request));
  });
}

template<> void ClassDef<Constructor<Directory>>::init() {
  super<Function>();
  ctor();
  accessor("defaultContentTypes", [](Object *, Value &ret) {
    Object *obj = Object::make();
    for (const auto &i : s_default_content_types) {
      obj->set(Str::make(i.first), Str::make(i.second));
    }
    ret.set(obj);
  });
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
  variable("Agent", class_of<Constructor<Agent>>());
  variable("Directory", class_of<Constructor<Directory>>());
  variable("File", class_of<Constructor<File>>());
}

} // namespace pjs
