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
#include "fstream.hpp"
#include "admin-service.hpp"
#include "admin-link.hpp"
#include "api/json.hpp"
#include "api/url.hpp"
#include "filters/tee.hpp"
#include "filters/pack.hpp"
#include "filters/mux.hpp"
#include "filters/http.hpp"
#include "filters/connect.hpp"

#include <syslog.h>

namespace pipy {
namespace logging {

//
// Logger
//

static Data::Producer s_dp("Logger");
static asio::io_context* s_io_context = nullptr;

std::set<Logger*> Logger::s_all_loggers;
AdminService* Logger::s_admin_service = nullptr;
AdminLink* Logger::s_admin_link = nullptr;

void Logger::init() {
  s_io_context = &Net::context();
}

void Logger::set_admin_service(AdminService *admin_service) {
  s_admin_service = admin_service;
}

void Logger::set_admin_link(AdminLink *admin_link) {
  static std::string s_tail("log/tail/");
  static std::string s_on("log/on/");
  static std::string s_off("log/off/");
  s_admin_link = admin_link;
  s_admin_link->add_handler(
    [](const std::string &command, const Data &payload) {
      if (utils::starts_with(command, s_tail)) {
        auto name = command.substr(s_tail.length());
        for (auto *logger : s_all_loggers) {
          if (logger->name()->str() == name) {
            logger->send_history();
          }
        }
        return true;
      } else {
        std::string name;
        bool enabled;
        if (utils::starts_with(command, s_on)) {
          name = command.substr(s_on.length());
          enabled = true;
        } else if (utils::starts_with(command, s_off)) {
          name = command.substr(s_off.length());
          enabled = false;
        }
        if (name.empty()) return false;
        for (auto *logger : s_all_loggers) {
          if (logger->name()->str() == name) {
            logger->enable_admin_link(enabled);
          }
        }
        return true;
      }
    }
  );
}

auto Logger::find(const std::string &name) -> Logger* {
  for (auto *logger : s_all_loggers) {
    if (logger->name()->str() == name) {
      return logger;
    }
  }
  return nullptr;
}

void Logger::shutdown_all() {
  for_each(
    [](Logger *logger) {
      logger->shutdown();
    }
  );
}

Logger::Logger(pjs::Str *name)
  : m_name(name)
{
  s_all_loggers.insert(this);
}

Logger::~Logger() {
  s_all_loggers.erase(this);
  while (auto *m = m_history.head()) {
    m_history.remove(m);
    delete m;
  }
}

void Logger::write(const Data &msg) {
  s_io_context->post(
    [=]() {
      write_async(msg);
    }
  );
}

void Logger::write_async(const Data &msg) {
  write_history(msg);
  if (!Net::is_running()) {
    for (const auto c : msg.chunks()) {
      auto ptr = std::get<0>(c);
      auto len = std::get<1>(c);
      std::cerr.write(ptr, len);
    }
    std::cerr << std::endl;
  } else if (InputContext::origin()) {
    write_internal(msg);
  } else {
    InputContext ic;
    write_internal(msg);
  }
}

void Logger::write_internal(const Data &msg) {
  Data msg_endl;
  s_dp.pack(&msg_endl, &msg);
  s_dp.push(&msg_endl, '\n');

  if (s_admin_service) {
    s_admin_service->write_log(m_name->str(), msg_endl);
  }

  if (s_admin_link && m_admin_link_enabled) {
    static std::string s_prefix("log/");
    Data buf;
    Data::Builder db(buf, &s_dp);
    db.push(s_prefix);
    db.push(m_name->str());
    db.push('\n');
    db.flush();
    buf.push(msg_endl);
    s_admin_link->send(buf);
  }

  for (const auto &p : m_targets) {
    p->write(msg);
  }
}

void Logger::write_history(const Data &msg) {
  m_history.push(new LogMessage(msg));
  m_history_size += msg.size();
  while (m_history_size > m_history_max) {
    auto *m = m_history.head();
    m_history.remove(m);
    m_history_size -= m->data.size();
    delete m;
  }
}

void Logger::tail(Data &buf) {
  for (auto *m = m_history.head(); m; m = m->next()) {
    s_dp.pack(&buf, &m->data);
    s_dp.push(&buf, '\n');
  }
}

void Logger::send_history() {
  if (s_admin_link) {
    static std::string s_prefix("log-tail/");
    Data buf;
    Data::Builder db(buf, &s_dp);
    db.push(s_prefix);
    db.push(m_name->str());
    db.push('\n');
    db.flush();
    tail(buf);
    s_admin_link->send(buf);
  }
}

void Logger::shutdown() {
  for (const auto &p : m_targets) {
    p->shutdown();
  }
}

Logger::StdoutTarget::StdoutTarget(FILE *f) {
  static Data::Producer s_dp("Logger::StdoutTarget");
  m_file_stream = FileStream::make(false, f, &s_dp);
}

void Logger::StdoutTarget::write(const Data &msg) {
  auto dp = m_file_stream->data_producer();
  Data *buf = Data::make();
  dp->push(buf, &msg);
  dp->push(buf, '\n');
  m_file_stream->input()->input(buf);
}

//
// Logger::FileTarget
//

Logger::FileTarget::FileTarget(pjs::Str *filename)
  : m_module(new Module)
{
  PipelineLayout *ppl = PipelineLayout::make();
  ppl->append(new Tee(filename));

  m_pipeline_layout = ppl;
  m_pipeline = Pipeline::make(ppl, new Context());
}

void Logger::FileTarget::write(const Data &msg) {
  static Data::Producer s_dp("Logger::FileTarget");
  Data *buf = Data::make();
  s_dp.push(buf, &msg);
  s_dp.push(buf, '\n');
  m_pipeline->input()->input(buf);
}

void Logger::FileTarget::shutdown() {
  m_module->shutdown();
  m_pipeline = nullptr;
}

//
// Logger::SyslogTarget
//

Logger::SyslogTarget::SyslogTarget(Priority priority) {
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
}

void Logger::SyslogTarget::write(const Data &msg) {
  auto len = msg.size();
  uint8_t buf[len+1];
  msg.to_bytes(buf);
  buf[len] = 0;
  syslog(m_priority, "%s", buf);
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
  batch = Pack::Options(batch_options, options_batch);
  tls = tls::Client::Options(tls_options, options_tls);
  Value(options, "method")
    .get(method)
    .check_nullable();
  Value(options, "headers")
    .get(headers)
    .check_nullable();
}

Logger::HTTPTarget::HTTPTarget(pjs::Str *url, const Options &options)
  : m_module(new Module)
{
  static Data::Producer s_dp("Logger::HTTPTarget");
  static pjs::ConstStr s_host("host");
  static pjs::ConstStr s_POST("POST");

  pjs::Ref<URL> url_obj = URL::make(url);
  bool is_tls = url_obj->protocol()->str() == "https:";

  m_mux_grouper = pjs::Method::make(
    "", [](pjs::Context &, pjs::Object *, pjs::Value &ret) {
      ret.set(pjs::Str::empty);
    }
  );

  PipelineLayout *ppl = PipelineLayout::make(m_module);
  PipelineLayout *ppl_pack = PipelineLayout::make(m_module);

  ppl->append(new Mux(pjs::Function::make(m_mux_grouper)))->add_sub_pipeline(ppl_pack);
  ppl_pack->append(new Pack(options.batch_size, options.batch));
  ppl_pack->append(new http::RequestEncoder(http::RequestEncoder::Options()));

  if (is_tls) {
    PipelineLayout *ppl_connect = PipelineLayout::make(m_module);
    ppl_pack->append(new tls::Client(options.tls))->add_sub_pipeline(ppl_connect);
    ppl_pack = ppl_connect;
  }

  ppl_pack->append(new Connect(url_obj->host(), Connect::Options()));

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
  head->method(options.method ? options.method.get() : s_POST.get());
  head->path(url_obj->path());
  head->headers(headers);
  m_message_start = MessageStart::make(head);
}

void Logger::HTTPTarget::write(const Data &msg) {
  m_pipeline = Pipeline::make(m_ppl, new Context());
  auto *input = m_pipeline->input();
  input->input(m_message_start);
  input->input(Data::make(msg));
  input->input(MessageEnd::make());
}

void Logger::HTTPTarget::shutdown() {
  m_module->shutdown();
  m_pipeline = nullptr;
}

//
// BinaryLogger
//

void BinaryLogger::log(int argc, const pjs::Value *args) {
  static Data::Producer s_dp("BinaryLogger");
  Data data;
  Data::Builder db(data, &s_dp);
  for (int i = 0; i < argc; i++) {
    auto &v = args[i];
    if (v.is<Data>()) {
      db.push(*v.as<Data>());
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
  static Data::Producer s_dp("TextLogger");
  Data data;
  Data::Builder db(data, &s_dp);
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
  static Data::Producer s_dp("JSONLogger");
  Data data;
  Data::Builder db(data, &s_dp);
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
    if (!ctx.arguments(1, &filename)) return;
    obj->as<Logger>()->add_target(new Logger::FileTarget(filename));
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
