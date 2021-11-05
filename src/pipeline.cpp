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
#include "logging.hpp"

namespace pipy {

//
// PipelineDef
//

List<PipelineDef> PipelineDef::s_all_pipeline_defs;

PipelineDef::PipelineDef(Module *module, Type type, const std::string &name)
  : m_module(module)
  , m_type(type)
  , m_name(name)
{
  s_all_pipeline_defs.push(this);
}

PipelineDef::~PipelineDef() {
  auto *ptr = m_pool;
  while (ptr) {
    auto *pipeline = ptr;
    ptr = ptr->m_next_free;
    delete pipeline;
  }
  s_all_pipeline_defs.remove(this);
}

void PipelineDef::bind() {
  for (const auto &f : m_filters) {
    f->bind();
  }
}

auto PipelineDef::append(Filter *filter) -> Filter* {
  m_filters.emplace_back(filter);
  filter->m_pipeline_def = this;
  return filter;
}

auto PipelineDef::alloc(Context *ctx) -> Pipeline* {
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
  m_active++;
  Log::debug("[pipeline %p] ++ name = %s, context = %llu", pipeline, m_name.c_str(), ctx->id());
  return pipeline;
}

void PipelineDef::free(Pipeline *pipeline) {
  pipeline->m_next_free = m_pool;
  m_pool = pipeline;
  m_active--;
  Log::debug("[pipeline %p] -- name = %s", pipeline, m_name.c_str());
  release();
}

//
// Pipeline
//

Pipeline::Pipeline(PipelineDef *def)
  : m_def(def)
{
  const auto &filters = def->m_filters;
  for (const auto &f : filters) {
    auto filter = f->clone();
    filter->m_pipeline_def = def;
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
    output(evt, f->input());
  } else {
    output(evt);
  }
}

void Pipeline::auto_release() {
  if (!m_auto_release) {
    retain();
    AutoReleasePool::add(this);
    m_auto_release = true;
  }
}

void Pipeline::finalize() {
  reset();
  m_def->free(this);
}

void Pipeline::reset() {
  chain(nullptr);
  for (auto f = m_filters.head(); f; f = f->next()) {
    f->reset();
  }
  m_context = nullptr;
}

//
// Pipeline::AutoReleasePool
//

Pipeline::AutoReleasePool* Pipeline::AutoReleasePool::s_stack = nullptr;

Pipeline::AutoReleasePool::AutoReleasePool() {
  m_next = s_stack;
  s_stack = this;
}

Pipeline::AutoReleasePool::~AutoReleasePool() {
  for (
    auto *p = m_pipelines;
    p; p = p->m_next_auto_release
  ) {
    p->release();
  }
  s_stack = m_next;
}

void Pipeline::AutoReleasePool::add(Pipeline *pipeline) {
  pipeline->m_next_auto_release = s_stack->m_pipelines;
  s_stack->m_pipelines = pipeline;
}

} // namespace pipy
