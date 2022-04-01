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

#include "use.hpp"
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "logging.hpp"

namespace pipy {

//
// Use
//

Use::Use(Module *module, pjs::Str *pipeline_name)
  : m_multiple(false)
  , m_pipeline_name(pipeline_name)
{
  m_modules.push_back(module);
}

Use::Use(
  const std::list<Module*> &modules,
  pjs::Str *pipeline_name,
  pjs::Function *turn_down
) : m_multiple(true)
  , m_modules(modules)
  , m_pipeline_name(pipeline_name)
  , m_turn_down(turn_down)
{
}

Use::Use(
  const std::list<Module*> &modules,
  pjs::Str *pipeline_name,
  pjs::Str *pipeline_name_down,
  pjs::Function *turn_down
) : m_multiple(true)
  , m_modules(modules)
  , m_pipeline_name(pipeline_name)
  , m_pipeline_name_down(pipeline_name_down)
  , m_turn_down(turn_down)
{
}

Use::Use(const Use &r)
  : m_multiple(r.m_multiple)
  , m_stages(r.m_stages)
  , m_pipeline_name(r.m_pipeline_name)
  , m_pipeline_name_down(r.m_pipeline_name_down)
  , m_turn_down(r.m_turn_down)
{
  Stage *next = nullptr;
  for (auto i = m_stages.rbegin(); i != m_stages.rend(); i++) {
    i->m_filter = this;
    i->m_next = next;
    next = &*i;
  }
  if (m_pipeline_name_down) {
    Stage *prev = nullptr;
    for (auto i = m_stages.begin(); i != m_stages.end(); i++) {
      i->m_prev = prev;
      prev = &*i;
    }
  } else {
    for (auto &s : m_stages) {
      s.m_prev = nullptr;
    }
  }
}

Use::~Use()
{
}

void Use::dump(std::ostream &out) {
  std::string module_name;
  if (m_modules.size() > 0) {
    module_name = m_modules.front()->path();
  } else {
    module_name = "(0 modules)";
  }
  if (m_modules.size() > 1) {
    module_name += " (plus ";
    module_name += std::to_string(m_modules.size() - 1);
    module_name += " more)";
  }
  out << "use " << module_name << " [" << m_pipeline_name->str() << ']';
}

void Use::bind() {
  Filter::bind();
  for (auto *mod : m_modules) {
    auto p = mod->find_named_pipeline(m_pipeline_name);
    if (!p && !m_multiple) {
      std::string msg("pipeline not found in module ");
      msg += mod->path();
      msg += ": ";
      msg += m_pipeline_name->str();
      throw std::runtime_error(msg);
    }
    PipelineDef *pd = nullptr;
    if (m_pipeline_name_down) {
      pd = mod->find_named_pipeline(m_pipeline_name_down);
    }
    if (p || pd) m_stages.emplace_back(p, pd);
  }
}

auto Use::clone() -> Filter* {
  return new Use(*this);
}

void Use::reset() {
  Filter::reset();
  for (auto &s : m_stages) {
    s.reset();
  }
}

void Use::process(Event *evt) {
  if (m_stages.empty()) {
    output(evt);
  } else {
    output(evt, m_stages.front().input());
  }
}

//
// Use::Stage
//

void Use::Stage::reset() {
  close();
  m_pipeline = nullptr;
  m_pipeline_down = nullptr;
  m_chained = false;
  m_turned_down = false;
}

void Use::Stage::on_event(Event *evt) {
  if (!m_chained) {
    m_chained = true;

    if (auto &when = m_filter->m_turn_down) {
      pjs::Value ret;
      if (!m_filter->callback(when, 0, nullptr, ret)) return;
      if (ret.to_boolean()) m_turned_down = true;
    }

    if (m_turned_down) {
      if (m_prev) {
        chain(m_prev->input_down());
      } else {
        chain(m_filter->output());
      }

    } else if (m_pipeline_def) {
      m_pipeline = Pipeline::make(m_pipeline_def, m_filter->context());
      chain(m_pipeline->input());
      if (m_next) {
        m_pipeline->chain(m_next->input());
      } else {
        m_pipeline->chain(input_down());
      }

    } else if (m_next) {
      chain(m_next->input());
    } else {
      chain(input_down());
    }
  }

  output(evt);
}

auto Use::Stage::input_down() -> EventTarget::Input* {
  if (m_pipeline_def_down) {
    auto *p = Pipeline::make(m_pipeline_def_down, m_filter->context());
    if (m_prev) {
      p->chain(m_prev->input_down());
    } else {
      p->chain(m_filter->output());
    }
    m_pipeline_down = p;
    return p->input();

  } else if (m_prev) {
    return m_prev->input_down();
  } else {
    return m_filter->output();
  }
}

} // namespace pipy
