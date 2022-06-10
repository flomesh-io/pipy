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

#include "fork.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "context.hpp"
#include "logging.hpp"

namespace pipy {

//
// Fork
//

Fork::Fork()
{
}

Fork::Fork(pjs::Object *initializers)
  : m_initializers(initializers)
{
}

Fork::Fork(const Fork &r)
  : Filter(r)
  , m_initializers(r.m_initializers)
{
}

Fork::~Fork() {
}

void Fork::dump(Dump &d) {
  Filter::dump(d);
  d.name = "fork";
}

auto Fork::clone() -> Filter* {
  return new Fork(*this);
}

void Fork::reset() {
  Filter::reset();
  if (m_pipelines) {
    m_pipelines->free();
    m_pipelines = nullptr;
  }
}

void Fork::process(Event *evt) {
  if (!m_pipelines) {
    auto mod = module();
    pjs::Value ret;
    pjs::Object *initializers = m_initializers.get();
    if (initializers && initializers->is_function()) {
      if (!callback(initializers->as<pjs::Function>(), 0, nullptr, ret)) return;
      if (!ret.is_array()) {
        Log::error("[fork] invalid initializer list");
        return;
      }
      initializers = ret.o();
    }
    if (initializers && initializers->is_array()) {
      auto arr = initializers->as<pjs::Array>();
      m_pipelines = pjs::PooledArray<pjs::Ref<Pipeline>>::make(arr->length());
      for (int i = 0; i < arr->length(); i++) {
        pjs::Value v;
        arr->get(i, v);
        auto pipeline = sub_pipeline(0, true);
        if (mod && v.is_object()) {
          auto context = pipeline->context();
          pjs::Object::assign(context->data(mod->index()), v.o());
        }
        m_pipelines->at(i) = pipeline;
      }
    } else {
      m_pipelines = pjs::PooledArray<pjs::Ref<Pipeline>>::make(1);
      auto pipeline = sub_pipeline(0, initializers ? true : false);
      if (initializers) {
        auto context = pipeline->context();
        pjs::Object::assign(context->data(mod->index()), initializers);
      }
      m_pipelines->at(0) = pipeline;
    }
  }

  if (m_pipelines) {
    for (int i = 0; i < m_pipelines->size(); i++) {
      auto out = m_pipelines->at(i)->input();
      output(evt->clone(), out);
    }
  }

  output(evt);
}

} // namespace pipy
