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

#include "logging.hpp"
#include "context.hpp"
#include "net.hpp"
#include "data.hpp"
#include "pipeline.hpp"
#include "input.hpp"
#include "fs.hpp"
#include "fstream.hpp"
#include "api/json.hpp"
#include "api/url.hpp"
#include "filters/tee.hpp"
#include "filters/pack.hpp"
#include "filters/mux.hpp"
#include "filters/http.hpp"
#include "filters/connect.hpp"

#ifndef _WIN32
#include <unistd.h>
#include <syslog.h>
#endif

namespace pipy {
namespace logging {

//
// Logger
//

static Data::Producer s_dp("Logger");
static Data::Producer s_dp_stdout("Logger::StdoutTarget");
static Data::Producer s_dp_binary("BinaryLogger");
static Data::Producer s_dp_text("TextLogger");
static Data::Producer s_dp_json("JSONLogger");

std::atomic<size_t> Logger::s_history_size(1024 * 1024);
std::atomic<int> Logger::s_history_sending_size(0);

void Logger::get_names(const std::function<void(const std::string &)> &cb) {
  History::for_each(
    [&](History *h) {
      cb(h->name());
    }
  );
}

bool Logger::tail(const std::string &name, Data &buffer) {
  return History::tail(name, buffer);
}

void Logger::close_all() {

}

Logger::Logger(pjs::Str *name)
  : m_name(name)
{
}

Logger::~Logger() {
  for (const auto &p : m_targets) {
    p->shutdown();
  }
}

void Logger::write(const Data &msg) {
  if (Net::main().is_running()) {
    if (s_history_sending_size < s_history_size) {
      auto name = m_name->data()->retain();
      auto sd = SharedData::make(msg)->retain();
      s_history_sending_size += msg.size();

      Net::main().post(
        [=]() {
          Data msg;
          sd->to_data(msg);
          s_history_sending_size -= msg.size();
          History::write(name->str(), msg);
          name->release();
          sd->release();
        }
      );
    }
  }

  InputContext ic;
  write_targets(msg);
}

void Logger::write_targets(const Data &msg) {
  for (const auto &p : m_targets) {
    p->write(msg);
  }
}

//
// Logger::History
//

std::map<std::string, Logger::History> Logger::History::s_all_histories;

void Logger::History::write(const std::string &name, const Data &msg) {
  auto &h = s_all_histories[name];
  if (h.m_name.empty()) h.m_name = name;
  h.write_message(msg);
}

bool Logger::History::tail(const std::string &name, Data &buffer) {
  auto p = s_all_histories.find(name);
  if (p == s_all_histories.end()) return false;
  p->second.dump_messages(buffer);
  return true;
}

void Logger::History::enable_streaming(const std::string &name, bool enabled) {
  auto p = s_all_histories.find(name);
  if (p != s_all_histories.end()) {
    p->second.m_streaming_enabled = enabled;
  }
}

void Logger::History::for_each(const std::function<void(History*)> &cb) {
  for (auto &i : s_all_histories) {
    cb(&i.second);
  }
}

void Logger::History::write_message(const Data &msg) {
  if (s_history_size == 0) return;

  if (m_buffer.size() != s_history_size) {
    m_buffer.resize(s_history_size);
    m_head = m_tail = 0;
  }

  while (m_head < m_tail && size() + msg.size() + 1 > m_buffer.size()) {
    auto p = m_head % m_buffer.size();
    while (m_head < m_tail) {
      m_head++;
      if (m_buffer[p++] == '\n') break;
      if (p == m_buffer.size()) p = 0;
    }
  }

  Data data(msg);
  while (data.size() > 0 && size() < m_buffer.size()) {
    auto p = m_tail % m_buffer.size();
    auto r = m_buffer.size() - p;
    auto n = std::min((int)r, data.size());
    Data part;
    data.shift(n, part);
    part.to_bytes(m_buffer.data() + p);
    m_tail += n;
  }

  if (data.empty() && size() + 1 <= m_buffer.size()) {
    m_buffer[m_tail++ % m_buffer.size()] = '\n';
  }
}

void Logger::History::dump_messages(Data &buffer) {
  if (m_buffer.size() > 0) {
    auto p = m_head;
    while (p < m_tail) {
      auto i = p % m_buffer.size();
      auto r = std::min(m_buffer.size() - i, m_tail - p);
      s_dp.push(&buffer, m_buffer.data() + i, r);
      p += r;
    }
  }
}

//
// Logger::StdoutTarget
//

Logger::StdoutTarget::~StdoutTarget() {
  if (m_file_stream) {
    m_file_stream->close();
  }
}

void Logger::StdoutTarget::write(const Data &msg) {
#ifndef _WIN32
  if (Net::current().is_running()) {
    if (!m_file_stream) {
      m_file_stream = FileStream::make(
        0,
        m_is_stderr ? os::FileHandle::std_error() : os::FileHandle::std_output(),
        &s_dp_stdout
      );
      m_file_stream->set_no_close();
    }
    Data *buf = Data::make();
    s_dp.push(buf, &msg);
    s_dp.push(buf, '\n');
    m_file_stream->input()->input(buf);
  } else
#endif // !_WIN32
  {
    std::ostream &os = (m_is_stderr ? std::cerr : std::cout);
    for (const auto c : msg.chunks()) {
      auto ptr = std::get<0>(c);
      auto len = std::get<1>(c);
      os.write(ptr, len);
    }
    os.put('\n');
  }
}

//
// Logger::FileTarget
//

Logger::FileTarget::FileTarget(pjs::Str *filename, const Options &options)
  : m_filename(pjs::Str::make(fs::abs_path(filename->str())))
  , m_options(options)
{
  PipelineLayout *ppl = PipelineLayout::make();
  ppl->append(new Tee(filename, options));
  m_pipeline_layout = ppl;
  m_pipeline = Pipeline::make(ppl, Context::make());
}

void Logger::FileTarget::write(const Data &msg) {
  Data *buf = Data::make();
  s_dp.push(buf, &msg);
  s_dp.push(buf, '\n');
  m_pipeline->input()->input(buf);
}

//
// Logger::SyslogTarget
//

Logger::SyslogTarget::SyslogTarget(Priority priority) {
#ifndef _WIN32
  switch (priority) {
    case Priority::EMERG   : m_priority = LOG_EMERG; break;
    case Priority::ALERT   : m_priority = LOG_ALERT; break;
    case Priority::CRIT    : m_priority = LOG_CRIT; break;
    case Priority::ERR     : m_priority = LOG_ERR; break;
    case Priority::WARNING : m_priority = LOG_WARNING; break;
    case Priority::NOTICE  : m_priority = LOG_NOTICE; break;
    case Priority::INFO    : m_priority = LOG_INFO; break;
    case Priority::DEBUG   : m_priority = LOG_DEBUG; break;
    default                : m_priority = LOG_INFO; break;
  }
#endif
}

void Logger::SyslogTarget::write(const Data &msg) {
  auto len = msg.size();
  pjs::vl_array<uint8_t, 1000> buf(len + 1);
  msg.to_bytes(buf);
  buf[len] = 0;
#ifdef _WIN32
  std::fprintf(stderr, "%s\n", (const char *)buf.data());
#else
  syslog(m_priority, "%s", (const char*)buf.data());
#endif
}

//
// Logger::HTTPTarget
//

Logger::HTTPTarget::Options::Options(pjs::Object *options) {
  const char *options_batch = "options.batch";
  const char *options_tls = "options.tls";
  pjs::Ref<pjs::Object> batch_options, tls_options;
  Value(options, "batch")
    .get(batch_options)
    .check_nullable();
  Value(batch_options, "size", options_batch)
    .get(batch_size)
    .check_nullable();
  Value(options, "bufferLimit")
    .get(buffer_limit)
    .check_nullable();
  batch = Pack::Options(batch_options, options_batch);
  tls = tls::Client::Options(tls_options, options_tls);
  Value(options, "method")
    .get(method)
    .check_nullable();
  Value(options, "headers")
    .get(headers)
    .check_nullable();
}

Logger::HTTPTarget::HTTPTarget(pjs::Str *url, const Options &options) {
  thread_local static pjs::ConstStr s_host("host");
  thread_local static pjs::ConstStr s_POST("POST");

  pjs::Ref<URL> url_obj = URL::make(url);
  bool is_tls = url_obj->protocol()->str() == "https:";

  m_mux_grouper = pjs::Method::make(
    "", [](pjs::Context &, pjs::Object *, pjs::Value &ret) {
      ret.set(pjs::Str::empty);
    }
  );

  PipelineLayout *ppl = PipelineLayout::make();
  PipelineLayout *ppl_pack = PipelineLayout::make();

  Mux::Options mux_opts;
  ppl->append(new Mux(pjs::Function::make(m_mux_grouper), mux_opts))->add_sub_pipeline(ppl_pack);
  ppl_pack->append(new Pack(options.batch_size, options.batch));
  ppl_pack->append(new http::RequestEncoder(http::RequestEncoder::Options()));

  if (is_tls) {
    PipelineLayout *ppl_connect = PipelineLayout::make();
    ppl_pack->append(new tls::Client(options.tls))->add_sub_pipeline(ppl_connect);
    ppl_pack = ppl_connect;
  }

  Connect::Options conn_opts;
  conn_opts.buffer_limit = options.buffer_limit;
  conn_opts.retry_delay = 5;
  conn_opts.retry_count = -1;
  ppl_pack->append(new Connect(url_obj->host(), conn_opts));

  m_ppl = ppl;

  auto *headers = pjs::Object::make();
  bool has_host = false;
  if (options.headers) {
    options.headers->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        if (utils::iequals(k->str(), s_host.get()->str())) has_host = true;
        headers->set(k, v);
      }
    );
  }
  if (!has_host) headers->set(s_host, url_obj->host());

  auto *head = http::RequestHead::make();
  head->method = options.method ? options.method.get() : s_POST.get();
  head->path = url_obj->path();
  head->headers = headers;
  m_message_start = MessageStart::make(head);
}

void Logger::HTTPTarget::write(const Data &msg) {
  m_pipeline = Pipeline::make(m_ppl, Context::make());
  auto *input = m_pipeline->input();
  input->input(m_message_start);
  input->input(Data::make(msg));
  input->input(MessageEnd::make());
}

void Logger::HTTPTarget::shutdown() {
  m_pipeline = nullptr;
}

//
// BinaryLogger
//

void BinaryLogger::log(int argc, const pjs::Value *args) {
  Data data;
  Data::Builder db(data, &s_dp_binary);
  for (int i = 0; i < argc; i++) {
    auto &v = args[i];
    if (v.is<Data>()) {
      Data buf(*v.as<Data>());
      db.push(std::move(buf));
    } else if (v.is_string()) {
      auto *s = v.s();
      db.push(s->c_str(), s->size());
    } else if (v.is_array()) {
      v.as<pjs::Array>()->iterate_all(
        [&](pjs::Value &v, int) {
          auto c = char(v.to_number());
          db.push(c);
        }
      );
    } else {
      auto *s = v.to_string();
      db.push(s->c_str(), s->size());
      s->release();
    }
  }
  db.flush();
  write(data);
}

//
// TextLogger
//

void TextLogger::log(int argc, const pjs::Value *args) {
  Data data;
  Data::Builder db(data, &s_dp_text);
  for (int i = 0; i < argc; i++) {
    auto &v = args[i];
    auto *s = v.to_string();
    if (i > 0) db.push(' ');
    db.push(s->str());
    s->release();
    if (v.is_object()) {
      db.push(':');
      JSON::encode(v, nullptr, 0, db);
    }
  }
  db.flush();
  write(data);
}

//
// JSONLogger
//

void JSONLogger::log(int argc, const pjs::Value *args) {
  Data data;
  Data::Builder db(data, &s_dp_json);
  for (int i = 0; i < argc; i++) {
    auto &v = args[i];
    JSON::encode(v, nullptr, 0, db);
  }
  db.flush();
  write(data);
}

} // namespace logging
} // namespace pipy

namespace pjs {

using namespace pipy::logging;

//
// Logger
//

template<> void EnumDef<Logger::SyslogTarget::Priority>::init() {
  define(Logger::SyslogTarget::Priority::EMERG, "EMERG");
  define(Logger::SyslogTarget::Priority::ALERT, "ALERT");
  define(Logger::SyslogTarget::Priority::CRIT, "CRIT");
  define(Logger::SyslogTarget::Priority::ERR, "ERR");
  define(Logger::SyslogTarget::Priority::WARNING, "WARNING");
  define(Logger::SyslogTarget::Priority::NOTICE, "NOTICE");
  define(Logger::SyslogTarget::Priority::INFO, "INFO");
  define(Logger::SyslogTarget::Priority::DEBUG, "DEBUG");
}

template<> void ClassDef<Logger>::init() {
  method("log", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Logger>()->log(ctx.argc(), &ctx.arg(0));
  });

  method("toStdout", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Logger>()->add_target(new Logger::StdoutTarget(stdout));
    ret.set(obj);
  });

  method("toStderr", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Logger>()->add_target(new Logger::StdoutTarget(stderr));
    ret.set(obj);
  });

  method("toFile", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Str *filename;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &filename, &options)) return;
    obj->as<Logger>()->add_target(new Logger::FileTarget(filename, options));
    ret.set(obj);
  });

  method("toSyslog", [](Context &ctx, Object *obj, Value &ret) {
    EnumValue<Logger::SyslogTarget::Priority> priority(Logger::SyslogTarget::Priority::INFO);
    if (!ctx.arguments(0, &priority)) return;
    obj->as<Logger>()->add_target(new Logger::SyslogTarget(priority));
    ret.set(obj);
  });

  method("toHTTP", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Str *url;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &url, &options)) return;
    obj->as<Logger>()->add_target(new Logger::HTTPTarget(url, options));
    ret.set(obj);
  });
}

//
// BinaryLogger
//

template<> void ClassDef<BinaryLogger>::init() {
  super<Logger>();

  ctor([](Context &ctx) -> Object* {
    pjs::Str *name;
    if (!ctx.arguments(1, &name)) return nullptr;
    return BinaryLogger::make(name);
  });
}

template<> void ClassDef<Constructor<BinaryLogger>>::init() {
  super<Function>();
  ctor();
}

//
// TextLogger
//

template<> void ClassDef<TextLogger>::init() {
  super<Logger>();

  ctor([](Context &ctx) -> Object* {
    pjs::Str *name;
    if (!ctx.arguments(1, &name)) return nullptr;
    return TextLogger::make(name);
  });
}

template<> void ClassDef<Constructor<TextLogger>>::init() {
  super<Function>();
  ctor();
}

//
// JSONLogger
//

template<> void ClassDef<JSONLogger>::init() {
  super<Logger>();

  ctor([](Context &ctx) -> Object* {
    pjs::Str *name;
    if (!ctx.arguments(1, &name)) return nullptr;
    return JSONLogger::make(name);
  });
}

template<> void ClassDef<Constructor<JSONLogger>>::init() {
  super<Function>();
  ctor();
}

//
// Logging
//

template<> void ClassDef<Logging>::init() {
  ctor();
  variable("BinaryLogger", class_of<Constructor<BinaryLogger>>());
  variable("TextLogger", class_of<Constructor<TextLogger>>());
  variable("JSONLogger", class_of<Constructor<JSONLogger>>());
}

} // namespace pjs
