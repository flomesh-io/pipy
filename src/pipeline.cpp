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

#include "pipeline.hpp"
#include "filter.hpp"
#include "context.hpp"
#include "message.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "log.hpp"

namespace pipy {

//
// PipelineLayout
//

thread_local List<PipelineLayout> PipelineLayout::s_all_pipeline_layouts;
thread_local size_t PipelineLayout::s_active_pipeline_count = 0;

PipelineLayout::PipelineLayout(Worker *worker, ModuleBase *module, int index, const std::string &name, const std::string &label)
  : m_index(index)
  , m_name(pjs::Str::make(name))
  , m_label(pjs::Str::make(label))
  , m_worker(worker)
  , m_module(module)
{
  s_all_pipeline_layouts.push(this);
  if (module) module->m_pipelines.push_back(this);
  Log::debug(Log::PIPELINE, "[pipeline] create layout: %s", name_or_label()->c_str());
}

PipelineLayout::~PipelineLayout() {
  Log::debug(Log::PIPELINE, "[pipeline] delete layout: %s", name_or_label()->c_str());
  auto *ptr = m_pool;
  while (ptr) {
    auto *pipeline = ptr;
    ptr = ptr->m_next_free;
    delete pipeline;
  }
  s_all_pipeline_layouts.remove(this);
}

void PipelineLayout::bind() {
  for (const auto &f : m_filters) {
    f->bind();
  }
}

void PipelineLayout::shutdown() {
  auto *p = m_pipelines.head();
  while (p) {
    p->shutdown();
    p = p->next();
  }

  for (const auto &f : m_filters) {
    f->shutdown();
  }
}

auto PipelineLayout::new_context() -> Context* {
  return m_module ? m_module->new_context() : m_worker->new_context();
}

auto PipelineLayout::name_or_label() const -> pjs::Str* {
  if (m_name != pjs::Str::empty) return m_name;
  if (m_label) return m_label;
  return pjs::Str::empty;
}

auto PipelineLayout::append(Filter *filter) -> Filter* {
  m_filters.emplace_back(filter);
  filter->m_pipeline_layout = this;
  return filter;
}

auto PipelineLayout::alloc(Context *ctx) -> Pipeline* {
  retain();
  Pipeline *pipeline = nullptr;
  if (m_pool) {
    pipeline = m_pool;
    m_pool = pipeline->m_next_free;
  } else {
    pipeline = new Pipeline(this);
    m_allocated++;
  }
  pipeline->m_context = ctx;
  pipeline->m_started = m_on_start ? false : true;
  m_pipelines.push(pipeline);
  m_active++;
  s_active_pipeline_count++;
  if (Log::is_enabled(Log::PIPELINE)) {
    Log::debug(
      Log::PIPELINE, "[pipeline] ++ %s, active = %d, pooled = %d, context = %llu",
      name_or_label()->c_str(),
      m_active,
      m_allocated - m_active,
      ctx->id()
    );
  }
  return pipeline;
}

void PipelineLayout::end(Pipeline *pipeline) {
  if (m_on_end) {
    auto &ctx = *pipeline->context();
    pjs::Value ret;
    (*m_on_end)(ctx, 0, nullptr, ret);
    if (!ctx.ok()) {
      Log::pjs_error(ctx.error());
      ctx.reset();
    }
  }
}

void PipelineLayout::free(Pipeline *pipeline) {
  m_pipelines.remove(pipeline);
  pipeline->m_next_free = m_pool;
  m_pool = pipeline;
  m_active--;
  s_active_pipeline_count--;
  if (Log::is_enabled(Log::PIPELINE)) {
    Log::debug(
      Log::PIPELINE, "[pipeline] -- %s, active = %d, pooled = %d",
      name_or_label()->c_str(),
      m_active,
      m_allocated - m_active
    );
  }
  release();
}

//
// Pipeline
//

Pipeline::Pipeline(PipelineLayout *layout)
  : m_layout(layout)
{
  const auto &filters = layout->m_filters;
  for (const auto &f : filters) {
    auto filter = f->clone();
    filter->m_pipeline_layout = layout;
    filter->m_pipeline = this;
    m_filters.push(filter);
  }
  if (auto f = m_filters.head()) {
    EventProxy::chain_forward(f->EventFunction::input());
    while (f) {
      auto n = f->next();
      f->EventFunction::chain(n ? n->EventFunction::input() : EventProxy::reply());
      f->chain();
      f->reset();
      f = n;
    }
  } else {
    EventProxy::chain_forward(EventProxy::reply());
  }
}

Pipeline::~Pipeline() {
  auto p = m_filters.head();
  while (p) {
    auto f = p;
    p = p->next();
    delete f;
  }
}

auto Pipeline::start(int argc, pjs::Value *argv) -> Pipeline* {
  if (auto *o = m_layout->m_on_start.get()) {
    pjs::Value starting_events;
    if (o->is<pjs::Function>()) {
      auto &ctx = *m_context;
      (*o->as<pjs::Function>())(ctx, argc, argv, starting_events);
      if (!ctx.ok()) {
        Log::pjs_error(ctx.error());
        ctx.reset();
        EventProxy::forward(StreamEnd::make(StreamEnd::RUNTIME_ERROR));
        return this;
      }
    } else {
      starting_events.set(o);
    }
    if (starting_events.is_promise()) {
      wait(starting_events.as<pjs::Promise>());
    } else {
      start(starting_events);
    }
  }
  return this;
}

void Pipeline::on_input(Event *evt) {
  if (m_started) {
    EventProxy::forward(evt);
  } else {
    m_pending_events.push(evt);
  }
}

void Pipeline::on_reply(Event *evt) {
  auto_release(this);
  EventProxy::output(evt);
}

void Pipeline::on_auto_release() {
  m_layout->end(this);
  reset();
  m_layout->free(this);
}

void Pipeline::wait(pjs::Promise *promise) {
  auto cb = StartingPromiseCallback::make(this);
  promise->then(nullptr, cb->resolved(), cb->rejected());
  m_starting_promise_callback = cb;
}

void Pipeline::resolve(const pjs::Value &value) {
  if (value.is_promise()) {
    m_starting_promise_callback->close();
    m_starting_promise_callback = nullptr;
    wait(value.as<pjs::Promise>());
  } else {
    start(value);
  }
}

void Pipeline::reject(const pjs::Value &value) {
  EventProxy::forward(StreamEnd::make(value));
}

void Pipeline::start(const pjs::Value &starting_events) {
  m_started = true;
  if (!starting_events.is_nullish()) {
    if (!Message::output(starting_events, EventProxy::input())) {
      char loc[100];
      Log::format_location(loc, sizeof(loc), m_layout->m_on_start_location, "onStart");
      char buf[300];
      auto len = std::snprintf(
        buf, sizeof(buf),
        "%s: initial input is not or did not return events or messages. "
        "Consider using void(...) if no initial input is intended", loc
      );
      Log::error("%s", buf);
      EventProxy::forward(StreamEnd::make(
        pjs::Error::make(pjs::Str::make(buf, len))
      ));
      m_pending_events.clear();
    }
  }
  if (!m_pending_events.empty()) {
    m_pending_events.flush(EventProxy::input());
  }
}

void Pipeline::shutdown() {
  for (auto f = m_filters.head(); f; f = f->next()) {
    f->shutdown();
  }
}

void Pipeline::reset() {
  AutoReleased::reset();
  EventFunction::close();
  EventProxy::chain(nullptr);
  for (auto f = m_filters.head(); f; f = f->next()) {
    f->reset();
  }
  m_context = nullptr;
  m_started = false;
  m_pending_events.clear();
  if (m_starting_promise_callback) {
    m_starting_promise_callback->close();
    m_starting_promise_callback = nullptr;
  }
}

//
// Pipeline::StartingPromiseCallback
//

void Pipeline::StartingPromiseCallback::on_resolved(const pjs::Value &value) {
  if (m_pipeline) {
    m_pipeline->resolve(value);
  }
}

void Pipeline::StartingPromiseCallback::on_rejected(const pjs::Value &error) {
  if (m_pipeline) {
    m_pipeline->reject(error);
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Pipeline::StartingPromiseCallback>::init() {
  super<Promise::Callback>();
}

} // namespace pjs
