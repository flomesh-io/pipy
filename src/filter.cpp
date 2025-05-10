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

#include "filter.hpp"
#include "pipeline.hpp"
#include "worker.hpp"
#include "message.hpp"
#include "log.hpp"

#include <cstdarg>

namespace pipy {

Filter::Filter()
  : m_subs(std::make_shared<std::vector<Sub>>())
  , m_buffer_stats(std::make_shared<BufferStats>())
{
}

Filter::Filter(const Filter &r)
  : m_subs(r.m_subs)
  , m_buffer_stats(r.m_buffer_stats)
  , m_location(r.m_location)
{
}

auto Filter::context() const -> Context* {
  if (m_pipeline) {
    return m_pipeline->context();
  } else {
    return nullptr;
  }
}

void Filter::set_location(const pjs::Location &loc) {
  m_location = loc;
  if (auto src = loc.source) {
    std::string buffer_name("Filter in ");
    buffer_name += src->filename;
    buffer_name += " at line ";
    buffer_name += std::to_string(loc.line);
    m_buffer_stats->name = buffer_name;
  }
}

void Filter::add_sub_pipeline(PipelineLayout *layout) {
  m_subs->emplace_back();
  m_subs->back().layout = layout;
}

void Filter::chain()
{
}

void Filter::reset()
{
}

void Filter::shutdown()
{
}

void Filter::dump(Dump &d)
{
}

auto Filter::sub_pipeline(
  int i,
  bool clone_context,
  Input *chain_to
) -> Pipeline* {

  auto layout = m_subs->at(i).layout.get();
  if (!layout) return nullptr;
  return sub_pipeline(layout, clone_context, chain_to);
}

auto Filter::sub_pipeline(
  PipelineLayout *layout,
  bool clone_context,
  Input *chain_to
) -> Pipeline* {

  auto ctx = m_pipeline->m_context.get();
  if (clone_context) {
    ctx = layout->worker()->new_context(ctx);
  }

  auto *p = Pipeline::make(layout, ctx);
  p->chain(pipeline()->chain(), pipeline()->chain_args());
  if (chain_to) p->chain(chain_to);

  return p;
}

void Filter::on_event(Event *evt) {
  Pipeline::auto_release(m_pipeline);
  process(evt);
}

void Filter::output(Message *msg) {
  msg->write(EventFunction::output());
}

void Filter::output(Message *msg, EventTarget::Input *input) {
  msg->write(input);
}

bool Filter::output(pjs::Object *obj) {
  return Message::output(obj, EventFunction::output());
}

bool Filter::output(pjs::Object *obj, EventTarget::Input *input) {
  return Message::output(obj, input);
}

bool Filter::callback(pjs::Function *func, int argc, pjs::Value argv[], pjs::Value &result) {
  auto c = context();
  (*func)(*c, argc, argv, result);
  if (c->ok()) return true;
  Log::pjs_error(c->error());
  error(pjs::Error::make(c->error()));
  c->reset();
  return false;
}

bool Filter::eval(pjs::Value &param, pjs::Value &result) {
  if (param.is_function()) {
    auto c = context();
    auto f = param.as<pjs::Function>();
    (*f)(*c, 0, nullptr, result);
    if (c->ok()) return true;
    Log::pjs_error(c->error());
    error(pjs::Error::make(c->error()));
    c->reset();
    return false;
  } else {
    result = param;
    return true;
  }
}

bool Filter::eval(pjs::Function *func, pjs::Value &result) {
  if (!func) return true;
  auto c = context();
  (*func)(*c, 0, nullptr, result);
  if (c->ok()) return true;
  Log::pjs_error(c->error());
  error(pjs::Error::make(c->error()));
  c->reset();
  return false;
}

void Filter::error(StreamEnd *end) {
  output(end);
}

void Filter::error(StreamEnd::Error type) {
  output(StreamEnd::make(type));
}

void Filter::error(pjs::Error *error) {
  output(StreamEnd::make(error));
}

void Filter::error(const char *format, ...) {
  char loc[1000], msg[1000], buf[2000];
  error_location(loc, sizeof(loc));
  va_list ap;
  va_start(ap, format);
  std::vsnprintf(msg, sizeof(msg), format, ap);
  va_end(ap);
  auto len = std::snprintf(buf, sizeof(buf), "%s: %s", loc, msg);
  Log::error("%s", buf);
  error(pjs::Error::make(pjs::Str::make(buf, len)));
}

auto Filter::error_location(char *buf, size_t len) -> size_t {
  Dump d; dump(d);
  return Log::format_location(buf, len, m_location, d.name.c_str());
}

} // namespace pipy
