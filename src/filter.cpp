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
#include "module.hpp"
#include "message.hpp"
#include "log.hpp"

#include <cstdarg>

namespace pipy {

Filter::Filter()
  : m_subs(std::make_shared<std::vector<Sub>>())
{
}

Filter::Filter(const Filter &r)
  : m_subs(r.m_subs)
  , m_location(r.m_location)
{
}

auto Filter::module() const -> ModuleBase* {
  if (m_pipeline_layout) {
    return m_pipeline_layout->module();
  } else {
    return nullptr;
  }
}

auto Filter::context() const -> Context* {
  if (m_pipeline) {
    return m_pipeline->context();
  } else {
    return nullptr;
  }
}

void Filter::add_sub_pipeline(PipelineLayout *layout) {
  m_subs->emplace_back();
  m_subs->back().layout = layout;
}

void Filter::add_sub_pipeline(pjs::Str *name) {
  m_subs->emplace_back();
  m_subs->back().name = name;
}

void Filter::add_sub_pipeline(int index) {
  m_subs->emplace_back();
  m_subs->back().index = index;
}

void Filter::bind() {
  for (auto &sub : *m_subs) {
    if (!sub.layout) {
      if (sub.name) {
        if (sub.name == pjs::Str::empty) {
          char loc[1000];
          std::string msg(loc, error_location(loc, sizeof(loc)));
          throw std::runtime_error(msg + ": empty pipeline name");
        } else {
          if (auto mod = dynamic_cast<JSModule*>(module())) {
            if (auto p = mod->find_named_pipeline(sub.name)) {
              sub.layout = p;
              continue;
            }
          }
          char loc[1000];
          std::string msg(loc, error_location(loc, sizeof(loc)));
          msg += ": pipeline not found with name: ";
          msg += sub.name->str();
          throw std::runtime_error(msg);
        }
      } else {
        if (auto mod = dynamic_cast<JSModule*>(module())) {
          if (auto p = mod->find_indexed_pipeline(sub.index)) {
            sub.layout = p;
            continue;
          }
        }
        char loc[1000];
        std::string msg(loc, error_location(loc, sizeof(loc)));
        msg += ": pipeline not found with index: ";
        msg += std::to_string(sub.index);
        throw std::runtime_error(msg);
      }
    }
  }
}

void Filter::chain() {
  if (auto f = next()) {
    EventFunction::chain(f->EventFunction::input());
  } else if (auto p = m_pipeline) {
    EventFunction::chain(p->EventFunction::output());
  }
}

void Filter::reset() {
  m_stream_end = false;
}

void Filter::shutdown()
{
}

void Filter::dump(Dump &d) {
  d.subs.resize(m_subs->size());
  for (size_t i = 0; i < d.subs.size(); i++) {
    auto &s = m_subs->at(i);
    auto &t = d.subs[i];
    t.index = s.index;
    if (s.name) {
      t.name = s.name->str();
    } else if (s.layout) {
      t.name = s.layout->name()->str();
    } else {
      t.name.clear();
    }
  }
  d.sub_type = d.subs.empty() ? Dump::NO_SUBS : Dump::BRANCH;
  d.out_type = d.subs.empty() ? Dump::OUTPUT_FROM_SELF : Dump::OUTPUT_FROM_SUBS;
}

auto Filter::sub_pipeline(
  int i,
  bool clone_context,
  Input *chain_to,
  Output *output_to
) -> Pipeline* {

  auto layout = m_subs->at(i).layout.get();
  if (!layout) return nullptr;
  return sub_pipeline(layout, clone_context, chain_to, output_to);
}

auto Filter::sub_pipeline(
  PipelineLayout *layout,
  bool clone_context,
  Input *chain_to,
  Output *output_to
) -> Pipeline* {

  auto ctx = m_pipeline->m_context.get();
  if (clone_context) {
    if (auto mod = module()) {
      ctx = mod->new_context(ctx);
    }
  }

  auto *p = Pipeline::make(layout, ctx);
  p->chain(pipeline()->chain());
  if (chain_to) p->chain(chain_to);
  if (output_to) p->output(output_to); else p->output(pipeline()->output());

  return p;
}

void Filter::on_event(Event *evt) {
  if (m_stream_end) return;
  if (evt->is<StreamEnd>()) m_stream_end = true;
  context()->group()->touch();
  Pipeline::auto_release(m_pipeline);
  process(evt);
}

void Filter::output(Event *evt) {
  EventFunction::output(evt);
}

bool Filter::output(const pjs::Value &evt) {
  if (!Message::output(evt, output())) {
    Log::error("[filter] output is not events or messages");
    return false;
  }
  return true;
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
  m_stream_end = true;
  output(end);
}

void Filter::error(StreamEnd::Error type) {
  m_stream_end = true;
  output(StreamEnd::make(type));
}

void Filter::error(pjs::Error *error) {
  m_stream_end = true;
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
  auto source = m_location.source;
  if (!source || source->filename.empty()) {
    return std::snprintf(
      buf, len,
      "%s() at line %d column %d",
      d.name.c_str(),
      m_location.line,
      m_location.column
    );
  } else {
    return std::snprintf(
      buf, len,
      "%s() at line %d column %d in %s",
      d.name.c_str(),
      m_location.line,
      m_location.column,
      m_location.source->filename.c_str()
    );
  }
}

} // namespace pipy
