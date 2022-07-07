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
#include "log.hpp"

namespace pipy {

//
// Fork
//

Fork::Fork()
{
}

Fork::Fork(const pjs::Value &init_arg)
  : m_init_arg(init_arg)
{
}

Fork::Fork(const Fork &r)
  : Filter(r)
  , m_init_arg(r.m_init_arg)
{
}

Fork::~Fork() {
}

void Fork::dump(Dump &d) {
  Filter::dump(d);
  d.name = "fork";
  d.out_type = Dump::OUTPUT_FROM_SELF;
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
    pjs::Value init_arg;
    if (!eval(m_init_arg, init_arg)) return;
    if (init_arg.is_array()) {
      auto arr = init_arg.as<pjs::Array>();
      auto len = arr->length();
      m_pipelines = pjs::PooledArray<pjs::Ref<Pipeline>>::make(len);
      for (int i = 0; i < len; i++) {
        pjs::Value v;
        arr->get(i, v);
        auto pipeline = sub_pipeline(0, true, nullptr, nullptr, 1, &v);
        m_pipelines->at(i) = pipeline;
      }
    } else {
      m_pipelines = pjs::PooledArray<pjs::Ref<Pipeline>>::make(1);
      auto pipeline = sub_pipeline(0, false, nullptr, nullptr, 1, &init_arg);
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
