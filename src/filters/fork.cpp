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
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "logging.hpp"

namespace pipy {

//
// Fork
//

Fork::Fork()
{
}

Fork::Fork(pjs::Str *target, pjs::Object *initializers)
  : m_target(target)
  , m_initializers(initializers)
{
}

Fork::Fork(const Fork &r)
  : m_pipeline(r.m_pipeline)
  , m_target(r.m_target)
  , m_initializers(r.m_initializers)
{
}

Fork::~Fork() {
}

auto Fork::help() -> std::list<std::string> {
  return {
    "fork(target[, initializers])",
    "Sends copies of events to other pipeline sessions",
    "target = <string> Name of the pipeline to send event copies to",
    "initializers = <array|function> Functions to initialize each session",
  };
}

void Fork::dump(std::ostream &out) {
  out << "fork";
}

auto Fork::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = true;
  return "fork";
}

void Fork::bind() {
  if (!m_pipeline) {
    m_pipeline = pipeline(m_target);
  }
}

auto Fork::clone() -> Filter* {
  return new Fork(*this);
}

void Fork::reset() {
  m_sessions = nullptr;
  m_session_end = false;
}

void Fork::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (!m_sessions) {
    auto root = static_cast<Context*>(ctx->root());
    auto mod = pipeline()->module();
    pjs::Value ret;
    pjs::Object *initializers = m_initializers.get();
    if (initializers && initializers->is_function()) {
      if (!callback(*ctx, initializers->as<pjs::Function>(), 0, nullptr, ret)) return;
      if (!ret.is_array()) {
        Log::error("[fork] invalid initializer list");
        abort();
        return;
      }
      initializers = ret.o();
    }
    if (initializers && initializers->is_array()) {
      m_sessions = initializers->as<pjs::Array>()->map(
        [&](pjs::Value &v, int, pjs::Value &ret) -> bool {
          auto context = root;
          if (v.is_object()) {
            context = mod->worker()->new_runtime_context(root);
            pjs::Object::assign(context->data(mod->index()), v.o());
          }
          auto session = Session::make(context, m_pipeline);
          ret.set(session);
          return true;
        }
      );
    } else {
      auto context = root;
      if (initializers) {
        context = mod->worker()->new_runtime_context(root);
        pjs::Object::assign(context->data(mod->index()), initializers);
      }
      auto session = Session::make(context, m_pipeline);
      m_sessions = pjs::Array::make(1);
      m_sessions->set(0, session);
    }
  }

  m_sessions->iterate_all([&](pjs::Value &v, int) {
    v.as<Session>()->input(inp->clone());
  });

  output(inp);

  if (inp->is<SessionEnd>()) {
    m_session_end = true;
  }
}

} // namespace pipy