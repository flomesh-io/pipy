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
#include "data.hpp"
#include "pipeline.hpp"
#include "input.hpp"
#include "admin-link.hpp"
#include "api/json.hpp"
#include "api/url.hpp"
#include "filters/tee.hpp"
#include "filters/pack.hpp"
#include "filters/mux.hpp"
#include "filters/http.hpp"
#include "filters/connect.hpp"

namespace pipy {
namespace logging {

//
// Logger
//

std::set<Logger*> Logger::s_all_loggers;
AdminLink* Logger::s_admin_link = nullptr;

Logger::Logger(pjs::Str *name)
  : m_name(name)
{
  s_all_loggers.insert(this);
}

Logger::~Logger() {
  s_all_loggers.erase(this);
}

void Logger::write(const Data &msg) {
  if (s_admin_link && m_admin_link_enabled) {
    static Data::Producer s_dp("Log Reports");
    static std::string s_prefix("log/");
    Data buf;
    Data::Builder db(buf, &s_dp);
    db.push(s_prefix);
    db.push(m_name->str());
    db.push('\n');
    db.flush();
    buf.push(msg);
    s_admin_link->send(buf);
  }

  for (const auto &p : m_targets) {
    p->write(msg);
  }
}

//
// Logger::FileTarget
//

Logger::FileTarget::FileTarget(pjs::Str *filename) {
  PipelineLayout *ppl = PipelineLayout::make(nullptr);
  ppl->append(new Tee(filename));

  m_pipeline_layout = ppl;
  m_pipeline = Pipeline::make(ppl, new Context());
}

void Logger::FileTarget::write(const Data &msg) {
  m_pipeline->input()->input(Data::make(msg));
}

//
// Logger::HTTPTarget
//

Logger::HTTPTarget::Options::Options(pjs::Object *options) {
  const char *options_batch = "options.batch";
  pjs::Ref<pjs::Object> batch;
  Value(options, "batch")
    .get(batch)
    .check_nullable();
  Value(batch, "size", options_batch)
    .get(size)
    .check_nullable();
  Value(batch, "timeout", options_batch)
    .get_seconds(timeout)
    .check_nullable();
  Value(batch, "interval", options_batch)
    .get_seconds(interval)
    .check_nullable();
  Value(batch, "head", options_batch)
    .get(head)
    .check_nullable();
  Value(batch, "tail", options_batch)
    .get(tail)
    .check_nullable();
  Value(batch, "separator", options_batch)
    .get(separator)
    .check_nullable();
  Value(options, "method")
    .get(method)
    .check_nullable();
  Value(options, "headers")
    .get(headers)
    .check_nullable();
}

Logger::HTTPTarget::HTTPTarget(pjs::Str *url, const Options &options) {
  pjs::Ref<URL> url_obj = URL::make(url);

  PipelineLayout *ppl = PipelineLayout::make(nullptr);
  PipelineLayout *ppl_connect = PipelineLayout::make(nullptr);

  Pack::Options pack_options;
  pack_options.timeout = options.timeout;
  pack_options.interval = options.interval;
  ppl->append(new Mux(nullptr, nullptr))->add_sub_pipeline(ppl_connect);
  ppl_connect->append(new Pack(options.size, pack_options));
  ppl_connect->append(new http::RequestEncoder(http::RequestEncoder::Options()));
  ppl_connect->append(new Connect(url_obj->host(), Connect::Options()));

  m_ppl = ppl;
  m_ppl_connect = ppl_connect;

  auto *head = http::RequestHead::make();
  static pjs::ConstStr s_POST("POST");
  head->method(s_POST);
  m_message_start = MessageStart::make(head);
}

void Logger::HTTPTarget::write(const Data &msg) {
  m_pipeline = Pipeline::make(m_ppl, new Context());
  auto *input = m_pipeline->input();
  InputContext ic;
  input->input(m_message_start);
  input->input(Data::make(msg));
  input->input(MessageEnd::make());
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
  db.push('\n');
  db.flush();
  write(data);
}

//
// JSONLogger
//

void JSONLogger::log(int argc, const pjs::Value *args) {
}

} // namespace logging
} // namespace pipy

namespace pjs {

using namespace pipy::logging;

//
// Logger
//

template<> void ClassDef<Logger>::init() {
  method("log", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Logger>()->log(ctx.argc(), &ctx.arg(0));
  });

  method("toFile", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    obj->as<Logger>()->add_target(new Logger::FileTarget(filename));
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
  variable("TextLogger", class_of<Constructor<TextLogger>>());
  variable("JSONLogger", class_of<Constructor<JSONLogger>>());
}

} // namespace pjs
