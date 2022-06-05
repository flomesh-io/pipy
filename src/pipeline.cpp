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
#include "logging.hpp"

namespace pipy {

//
// PipelineLayout
//

List<PipelineLayout> PipelineLayout::s_all_pipeline_layouts;

PipelineLayout::PipelineLayout(Module *module, Type type, const std::string &name)
  : m_type(type)
  , m_name(pjs::Str::make(name))
  , m_module(module)
{
  s_all_pipeline_layouts.push(this);
  Log::debug("[p-layout %p] ++ name = %s", this, m_name->c_str());
}

PipelineLayout::~PipelineLayout() {
  Log::debug("[p-layout %p] -- name = %s", this, m_name->c_str());
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
  Log::debug("[pipeline %p] ++ name = %s, context = %llu", pipeline, m_name->c_str(), ctx->id());
  return pipeline;
}

void PipelineLayout::free(Pipeline *pipeline) {
  m_pipelines.remove(pipeline);
  pipeline->m_next_free = m_pool;
  m_pool = pipeline;
  Log::debug("[pipeline %p] -- name = %s", pipeline, m_name->c_str());
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

void Pipeline::on_recycle() {
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
