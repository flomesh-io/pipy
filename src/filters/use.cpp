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
#include "worker.hpp"
#include "pipeline.hpp"
#include "log.hpp"
#include "pipy/nmi.h"

#include <dlfcn.h>

namespace pipy {

//
// Use
//

Use::Use(JSModule *module, pjs::Str *pipeline_name)
  : m_multiple(false)
  , m_pipeline_name(pipeline_name)
{
  m_modules.push_back(module);
}

Use::Use(nmi::NativeModule *module, pjs::Str *pipeline_name)
  : m_native(true)
  , m_multiple(false)
  , m_native_module(module)
  , m_pipeline_name(pipeline_name)
{
}

Use::Use(
  const std::list<JSModule*> &modules,
  pjs::Str *pipeline_name,
  pjs::Function *turn_down
) : m_multiple(true)
  , m_modules(modules)
  , m_pipeline_name(pipeline_name)
  , m_turn_down(turn_down)
{
}

Use::Use(
  const std::list<JSModule*> &modules,
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
  : m_native(r.m_native)
  , m_multiple(r.m_multiple)
  , m_native_pipeline_layout(r.m_native_pipeline_layout)
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

void Use::dump(Dump &d) {
  Filter::dump(d);
  if (m_native) {
    d.name = "use ";
    d.name += m_native_module->filename()->str();
    if (m_pipeline_name) {
      d.name += " [";
      d.name += m_pipeline_name->str();
      d.name += ']';
    }
  } else {
    std::string module_name;
    if (m_modules.size() > 0) {
      module_name = m_modules.front()->filename()->str();
    } else {
      module_name = "(0 modules)";
    }
    if (m_modules.size() > 1) {
      module_name += " (plus ";
      module_name += std::to_string(m_modules.size() - 1);
      module_name += " more)";
    }
    d.name = "use ";
    d.name += module_name;
    if (m_pipeline_name) {
      d.name += " [";
      d.name += m_pipeline_name->str();
      d.name += ']';
    }
  }
}

void Use::bind() {
  Filter::bind();
  if (m_native) {
    m_native_pipeline_layout = m_native_module->pipeline_layout(m_pipeline_name);
    if (!m_native_pipeline_layout) {
      if (m_pipeline_name) {
        std::string msg("cannot find pipeline with name ");
        throw std::runtime_error(msg + m_pipeline_name->str());
      } else {
        std::string msg("cannot find the entry pipeline in native module ");
        throw std::runtime_error(msg + m_native_module->filename()->str());
      }
    }

  } else {
    for (auto *mod : m_modules) {
      auto p = m_pipeline_name
        ? mod->find_named_pipeline(m_pipeline_name)
        : mod->entrance_pipeline();
      if (!p && !m_multiple) {
        std::string msg("pipeline not found in module ");
        msg += mod->filename()->str();
        if (m_pipeline_name) {
          msg += ": ";
          msg += m_pipeline_name->str();
        }
        throw std::runtime_error(msg);
      }
      PipelineLayout *pd = nullptr;
      if (m_pipeline_name_down) {
        pd = mod->find_named_pipeline(m_pipeline_name_down);
      }
      if (p || pd) m_stages.emplace_back(p, pd);
    }
  }
}

auto Use::clone() -> Filter* {
  return new Use(*this);
}

void Use::reset() {
  Filter::reset();
  if (m_native_pipeline) {
    m_native_pipeline->release();
    m_native_pipeline = nullptr;
  }
  for (auto &s : m_stages) {
    s.reset();
  }
}

void Use::process(Event *evt) {
  if (m_native) {
    if (!m_native_pipeline) {
      m_native_pipeline = nmi::Pipeline::make(m_native_pipeline_layout, Filter::context(), Filter::output());
    }
    m_native_pipeline->input(evt);
  } else {
    if (m_stages.empty()) {
      output(evt);
    } else {
      output(evt, m_stages.front().input());
    }
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

    } else if (m_pipeline_layout) {
      auto p = Pipeline::make(m_pipeline_layout, m_filter->context());
      chain(p->input());
      if (m_next) {
        p->chain(m_next->input());
      } else {
        p->chain(input_down());
      }
      m_pipeline = p;
      p->start();

    } else if (m_next) {
      chain(m_next->input());
    } else {
      chain(input_down());
    }
  }

  output(evt);
}

auto Use::Stage::input_down() -> EventTarget::Input* {
  if (m_pipeline_layout_down) {
    auto *p = Pipeline::make(m_pipeline_layout_down, m_filter->context());
    if (m_prev) {
      p->chain(m_prev->input_down());
    } else {
      p->chain(m_filter->output());
    }
    m_pipeline_down = p;
    p->start();
    return p->input();

  } else if (m_prev) {
    return m_prev->input_down();
  } else {
    return m_filter->output();
  }
}

} // namespace pipy
