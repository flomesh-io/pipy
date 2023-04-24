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
#include "module.hpp"
#include "log.hpp"

namespace pipy {

//
// PipelineLayout
//

thread_local List<PipelineLayout> PipelineLayout::s_all_pipeline_layouts;
thread_local size_t PipelineLayout::s_active_pipeline_count = 0;

PipelineLayout::PipelineLayout(ModuleBase *module, int index, const std::string &name, const std::string &label)
  : m_index(index)
  , m_name(pjs::Str::make(name))
  , m_label(pjs::Str::make(label))
  , m_module(module)
{
  s_all_pipeline_layouts.push(this);
  if (module) module->m_pipelines.push_back(this);
  Log::debug(Log::ALLOC, "[p-layout %p] ++ name = %s", this, name_or_label()->c_str());
}

PipelineLayout::~PipelineLayout() {
  Log::debug(Log::ALLOC, "[p-layout %p] -- name = %s", this, name_or_label()->c_str());
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
  return m_module ? m_module->new_context() : Context::make();
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
  m_pipelines.push(pipeline);
  s_active_pipeline_count++;
  if (Log::is_enabled(Log::ALLOC)) {
    Log::debug(Log::ALLOC, "[pipeline %p] ++ name = %s, context = %llu", pipeline, name_or_label()->c_str(), ctx->id());
  }
  return pipeline;
}

void PipelineLayout::start(Pipeline *pipeline, int argc, pjs::Value *argv) {
  static const std::string s_invalid_input_events(
    "initial input is not or did not return events or messages. "
    "Consider using void(...) if no initial input is intended"
  );
  if (auto *o = m_on_start.get()) {
    pjs::Value starting_events;
    if (o->is<pjs::Function>()) {
      auto &ctx = *pipeline->context();
      (*o->as<pjs::Function>())(ctx, argc, argv, starting_events);
      if (!ctx.ok()) {
        pipeline->input()->input(StreamEnd::make(StreamEnd::RUNTIME_ERROR));
        return;
      }
    } else {
      starting_events.set(o);
    }
    if (!starting_events.is_nullish()) {
      if (!Message::output(starting_events, pipeline->input())) {
        const auto &loc = m_on_start_location;
        char buf[200];
        auto len = (!loc.source || loc.source->filename.empty() ?
          std::snprintf(
            buf, sizeof(buf),
            "onStart() at line %d column %d: %s",
            loc.line,
            loc.column,
            s_invalid_input_events.c_str()
          ) :
          std::snprintf(
            buf, sizeof(buf),
            "onStart() at line %d column %d in %s: %s",
            loc.line,
            loc.column,
            loc.source->filename.c_str(),
            s_invalid_input_events.c_str()
          )
        );
        Log::error("%s", buf);
        pipeline->input()->input(StreamEnd::make(
          pjs::Error::make(pjs::Str::make(buf, len))
        ));
      }
    }
  }
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
  s_active_pipeline_count--;
  if (Log::is_enabled(Log::ALLOC)) {
    Log::debug(Log::ALLOC, "[pipeline %p] -- name = %s", pipeline, name_or_label()->c_str());
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
  for (auto f = m_filters.head(); f; f = f->next()) {
    f->chain();
    f->reset();
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

void Pipeline::chain(Input *input) {
  EventFunction::chain(input);
  if (auto f = m_filters.tail()) {
    f->chain();
  }
}

void Pipeline::on_event(Event *evt) {
  if (auto f = m_filters.head()) {
    EventFunction::output(evt, f->input());
  } else {
    EventFunction::output(evt);
  }
}

void Pipeline::on_auto_release() {
  m_layout->end(this);
  reset();
  m_layout->free(this);
}

void Pipeline::shutdown() {
  for (auto f = m_filters.head(); f; f = f->next()) {
    f->shutdown();
  }
}

void Pipeline::reset() {
  PipelineBase::reset();
  EventTarget::close();
  EventFunction::chain(nullptr);
  for (auto f = m_filters.head(); f; f = f->next()) {
    f->reset();
  }
  m_context = nullptr;
}

} // namespace pipy
