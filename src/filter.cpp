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
#include "logging.hpp"

#include <sstream>

namespace pipy {

Filter::Filter()
  : m_subs(std::make_shared<std::vector<Sub>>())
{
}

Filter::Filter(const Filter &r)
  : m_subs(r.m_subs)
{
}

auto Filter::module() const -> Module* {
  if (m_pipeline_def) {
    return m_pipeline_def->module();
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

void Filter::add_sub_pipeline(PipelineDef *def) {
  m_subs->emplace_back();
  m_subs->back().def = def;
}

void Filter::add_sub_pipeline(pjs::Str *name) {
  m_subs->emplace_back();
  m_subs->back().name = name;
}

auto Filter::get_sub_pipeline_name(int i) -> const std::string& {
  static std::string empty;
  auto &sub = m_subs->at(i);
  if (sub.name) {
    return sub.name->str();
  } else if (sub.def) {
    return sub.def->name()->str();
  } else {
    return empty;
  }
}

void Filter::bind() {
  for (auto &sub : *m_subs) {
    if (sub.name && sub.name != pjs::Str::empty && !sub.def) {
      if (auto mod = module()) {
        if (auto p = mod->find_named_pipeline(sub.name)) {
          sub.def = p;
          continue;
        }
      }
      std::string msg("pipeline not found: ");
      msg += sub.name->str();
      throw std::runtime_error(msg);
    }
  }
}

void Filter::chain() {
  if (auto f = next()) {
    EventFunction::chain(f->EventFunction::input());
  } else if (auto p = m_pipeline) {
    EventFunction::chain(p->output());
  }
}

void Filter::reset() {
  m_stream_end = false;
}

void Filter::shutdown()
{
}

auto Filter::sub_pipeline(int i, bool clone_context) -> Pipeline* {
  auto def = m_subs->at(i).def.get();
  if (!def) return nullptr;

  auto ctx = m_pipeline->m_context.get();
  if (clone_context) {
    if (auto mod = module()) {
      ctx = mod->worker()->new_runtime_context(ctx);
    }
  }

  return Pipeline::make(def, ctx);
}

void Filter::on_event(Event *evt) {
  if (m_stream_end) return;
  if (evt->is<StreamEnd>()) m_stream_end = true;
  Pipeline::auto_release(m_pipeline);
  process(evt);
}

bool Filter::output(const pjs::Value &evt) {
  if (evt.is_instance_of(pjs::class_of<Event>())) {
    output(evt.as<Event>());
    return true;
  } else if (evt.is_instance_of(pjs::class_of<Message>())) {
    auto *msg = evt.as<Message>();
    auto *body = msg->body();
    output(MessageStart::make(msg->head()));
    if (body) output(body);
    output(MessageEnd::make(msg->tail()));
    return true;
  } else if (evt.is_array()) {
    auto *a = evt.as<pjs::Array>();
    auto last = a->iterate_while([&](pjs::Value &v, int i) -> bool {
      if (v.is_instance_of(pjs::class_of<Event>())) {
        output(v.as<Event>());
        return true;
      } else if (v.is_instance_of(pjs::class_of<Message>())) {
        auto *msg = v.as<Message>();
        auto *body = msg->body();
        output(MessageStart::make(msg->head()));
        if (body) output(body);
        output(MessageEnd::make(msg->tail()));
        return true;
      } else {
        return v.is_null() || v.is_undefined();
      }
    });
    if (last < a->length()) {
      Log::error("[filter] not an Event object");
      return false;
    }
    return true;
  } else if (evt.is_null() || evt.is_undefined()) {
    return true;
  } else {
    Log::error("[filter] not an Event object");
    return false;
  }
}

bool Filter::callback(pjs::Function *func, int argc, pjs::Value argv[], pjs::Value &result) {
  auto c = m_pipeline->m_context.get();
  (*func)(*c, argc, argv, result);
  if (c->ok()) return true;
  auto mod = m_pipeline_def->module();
  Log::pjs_error(c->error(), mod->source());
  c->reset();
  return false;
}

bool Filter::eval(pjs::Value &param, pjs::Value &result) {
  if (param.is_function()) {
    auto c = m_pipeline->m_context.get();
    auto f = param.as<pjs::Function>();
    (*f)(*c, 0, nullptr, result);
    if (c->ok()) return true;
    auto mod = m_pipeline_def->module();
    Log::pjs_error(c->error(), mod->source());
    c->reset();
    return false;
  } else {
    result = param;
    return true;
  }
}

bool Filter::eval(pjs::Function *func, pjs::Value &result) {
  if (!func) return true;
  auto c = m_pipeline->m_context.get();
  (*func)(*c, 0, nullptr, result);
  if (c->ok()) return true;
  auto mod = m_pipeline_def->module();
  Log::pjs_error(c->error(), mod->source());
  c->reset();
  return false;
}

} // namespace pipy
