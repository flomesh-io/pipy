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

#include "link.hpp"
#include "data.hpp"
#include "pipeline.hpp"

namespace pipy {

//
// Link
//

Link::Link()
  : m_conditions(std::make_shared<std::vector<Condition>>())
{
}

Link::Link(const Link &r)
  : Filter(r)
  , m_conditions(r.m_conditions)
{
}

Link::~Link()
{
}

void Link::dump(std::ostream &out) {
  out << "link";
}

void Link::add_condition(pjs::Function *func) {
  m_conditions->emplace_back();
  m_conditions->back().func = func;
}

void Link::add_condition(const std::function<bool()> &func) {
  m_conditions->emplace_back();
  m_conditions->back().cpp_func = func;
}

auto Link::clone() -> Filter* {
  return new Link(*this);
}

void Link::reset() {
  Filter::reset();
  m_buffer.clear();
  m_pipeline = nullptr;
  m_chosen = false;
}

void Link::process(Event *evt) {
  if (!m_chosen) {
    if (auto data = evt->as<Data>()) {
      if (data->empty()) {
        return;
      }
    }
    const auto &conditions = *m_conditions;
    for (int i = 0; i < conditions.size(); i++) {
      const auto &cond = conditions[i];
      if (cond.func) {
        pjs::Value ret;
        if (!callback(cond.func, 0, nullptr, ret)) return;
        m_chosen = ret.to_boolean();
      } else if (cond.cpp_func) {
        m_chosen = cond.cpp_func();
      } else {
        m_chosen = true;
      }
      if (m_chosen) {
        if (auto *pipeline = sub_pipeline(i, false)) {
          pipeline->chain(output());
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
