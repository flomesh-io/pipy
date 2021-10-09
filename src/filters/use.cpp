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
#include "session.hpp"
#include "logging.hpp"

namespace pipy {

//
// Use
//

Use::Use()
{
}

Use::Use(Module *module, pjs::Str *pipeline_name, pjs::Function *when)
  : m_multiple(false)
  , m_pipeline_name(pipeline_name)
  , m_when(when)
{
  m_modules.push_back(module);
}

Use::Use(const std::list<Module*> &modules, pjs::Str *pipeline_name, pjs::Function *when)
  : m_multiple(true)
  , m_modules(modules)
  , m_pipeline_name(pipeline_name)
  , m_when(when)
{
}

Use::Use(const Use &r)
  : m_stages(r.m_stages)
  , m_multiple(r.m_multiple)
  , m_pipeline_name(r.m_pipeline_name)
  , m_when(r.m_when)
{
  Stage *next = nullptr;
  for (auto i = m_stages.rbegin(); i != m_stages.rend(); i++) {
    i->m_use = this;
    i->m_next = next;
    next = &*i;
  }
}

Use::~Use()
{
}

auto Use::help() -> std::list<std::string> {
  return {
    "use(modules, pipeline[, when])",
    "Sends events to pipelines in different modules",
    "modules = <string|array> Filenames of the modules",
    "pipeline = <string> Name of the pipeline",
    "when = <function> Callback function that returns false to bypass",
  };
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
  for (auto *mod : m_modules) {
    auto p = mod->find_named_pipeline(m_pipeline_name);
    if (!p && !m_multiple) {
      std::string msg("pipeline not found in module ");
      msg += mod->path();
      msg += ": ";
      msg += m_pipeline_name->str();
      throw std::runtime_error(msg);
    }
    if (p) m_stages.emplace_back(p);
  }
}

auto Use::clone() -> Filter* {
  return new Use(*this);
}

void Use::reset() {
  for (auto &s : m_stages) {
    s.reset();
  }
  m_session_end = false;
}

void Use::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (m_stages.empty()) {
    output(inp);

  } else {
    m_stages.front().use(ctx, inp);
  }

  if (inp->is<SessionEnd>()) m_session_end = true;
}

void Use::Stage::use(Context *context, Event *inp) {
  if (!m_session && !m_bypass) {
    auto &when = m_use->m_when;
    if (when) {
      pjs::Value ret;
      if (!m_use->callback(*context, when, 0, nullptr, ret)) return;
      if (!ret.to_boolean()) m_bypass = true;
    }

    if (!m_bypass) {
      m_session = Session::make(context, m_pipeline);
      if (m_next) {
        auto next = m_next;
        m_session->on_output(
          [=](Event *inp) {
            next->use(context, inp);
          }
        );
      } else {
        m_session->on_output(m_use->out());
      }
    }
  }

  if (m_session) {
    m_session->input(inp);
  } else {
    m_use->output(inp);
  }
}

} // namespace pipy