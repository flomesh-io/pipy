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

#include "branch.hpp"
#include "pipeline.hpp"

namespace pipy {

//
// Branch
//

Branch::Branch(int count, pjs::Function **conds, const pjs::Value *layout)
  : m_conditions(std::make_shared<std::vector<Condition>>())
{
  m_conditions->resize(count);
  for (int i = 0; i < count; i++) {
    m_conditions->at(i).func = conds[i];
    if (layout[i].is_number()) {
      add_sub_pipeline(layout[i].n());
    } else if (layout[i].is_string()) {
      add_sub_pipeline(layout[i].s());
    } else {
      add_sub_pipeline(pjs::Str::empty);
    }
  }
}

Branch::Branch(const Branch &r)
  : Filter(r)
  , m_conditions(r.m_conditions)
{
}

Branch::~Branch()
{
}

void Branch::dump(Dump &d) {
  Filter::dump(d);
  d.name = "branch";
}

auto Branch::clone() -> Filter* {
  return new Branch(*this);
}

void Branch::reset() {
  Filter::reset();
  m_buffer.clear();
  m_pipeline = nullptr;
  m_chosen = false;
}

void Branch::process(Event *evt) {
  if (!m_chosen) {
    const auto &conditions = *m_conditions;
    for (int i = 0; i < conditions.size(); i++) {
      const auto &cond = conditions[i];
      if (cond.func) {
        pjs::Value ret;
        if (!eval(cond.func, ret)) return;
        m_chosen = ret.to_boolean();
      } else {
        m_chosen = true;
      }
      if (m_chosen) {
        if (auto *pipeline = sub_pipeline(i, false, output())) {
          pipeline->start();
          m_pipeline = pipeline;
          m_buffer.flush([&](Event *evt) {
            output(evt, pipeline->input());
          });
        } else {
          m_buffer.flush([&](Event *evt) {
            output(evt);
          });
        }
        break;
      }
    }
  }

  if (!m_chosen) {
    m_buffer.push(evt);
  } else if (auto *p = m_pipeline.get()) {
    output(evt, p->input());
  } else {
    output(evt);
  }
}

} // namespace pipy
