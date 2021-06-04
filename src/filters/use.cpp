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

Use::Use(Module *module, pjs::Str *pipeline_name, pjs::Object *argv)
  : m_module(module)
  , m_pipeline_name(pipeline_name)
  , m_argv(argv)
{
}

Use::Use(const Use &r)
  : m_module(r.m_module)
  , m_pipeline_name(r.m_pipeline_name)
  , m_argv(r.m_argv)
{
}

Use::~Use()
{
}

auto Use::help() -> std::list<std::string> {
  return {
    "use(module, pipeline[, argv...])",
    "Sends events to a pipeline in a different module",
    "module = <string> Filename of the module",
    "pipeline = <string> Name of the pipeline",
    "argv = <array|function> Arguments that are visible from the new session with __argv",
  };
}

void Use::dump(std::ostream &out) {
  out << "use " << m_module->path() << " [" << m_pipeline_name->str() << ']';
}

auto Use::clone() -> Filter* {
  return new Use(*this);
}

void Use::reset() {
  m_session = nullptr;
  m_session_end = false;
}

void Use::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (!m_session) {
    auto root = static_cast<Context*>(ctx->root());
    if (auto pipeline = m_module->find_named_pipeline(m_pipeline_name)) {
      auto session = Session::make(root, pipeline);
      if (m_argv) {
        if (m_argv->is_function()) {
          pjs::Value ret;
          if (!callback(*ctx, m_argv->as<pjs::Function>(), 0, nullptr, ret)) return;
          if (!ret.is_array()) {
            auto a = pjs::Array::make(1);
            a->set(0, ret);
            ret = a;
          }
          ctx->data(m_module->index())->argv(ret.as<pjs::Array>());
        } else if (m_argv->is_array()) {
          ctx->data(m_module->index())->argv(m_argv->as<pjs::Array>());
        }
      }
      session->on_output(out());
      m_session = session;
    } else {
      Log::error("[use] unknown pipeline: %s in %s",
        m_pipeline_name->c_str(),
        m_module->path().c_str()
      );
    }
  }

  if (m_session) {
    m_session->input(inp);
  }

  if (inp->is<SessionEnd>()) m_session_end = true;
}

} // namespace pipy