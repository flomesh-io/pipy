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

Fork::Fork(pjs::Str *target, pjs::Object *session_data)
  : m_target(target)
  , m_session_data(session_data)
{
}

Fork::Fork(const Fork &r)
  : Fork(r.m_target, r.m_session_data)
{
}

Fork::~Fork() {
}

auto Fork::help() -> std::list<std::string> {
  return {
    "fork(target[, sessionData])",
    "Sends copies of events to other pipeline sessions",
    "target = <string> Name of the pipeline to send event copies to",
    "sessionData = <object/array/function> Data of `this` in the forked sessions",
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
    if (auto pipeline = mod->find_named_pipeline(m_target)) {
      pjs::Value ret;
      pjs::Object *session_data = m_session_data.get();
      if (session_data && session_data->is_function()) {
        if (!callback(*ctx, session_data->as<pjs::Function>(), 0, nullptr, ret)) return;
        if (!ret.is_object()) {
          Log::error("[fork] invalid session data");
          abort();
          return;
        }
        session_data = ret.o();
      }
      if (session_data && session_data->is_array()) {
        m_sessions = session_data->as<pjs::Array>()->map(
          [&](pjs::Value &v, int, pjs::Value &ret) -> bool {
            auto session = Session::make(root, pipeline);
            if (v.is_object()) pjs::Object::assign(session, v.o());
            ret.set(session);
            return true;
          }
        );
      } else {
        auto session = Session::make(root, pipeline);
        pjs::Object::assign(session, session_data);
        m_sessions = pjs::Array::make(1);
        m_sessions->set(0, session);
      }
    } else {
      Log::error("[fork] unknown pipeline: %s", m_target->c_str());
      m_session_end = true;
      return;
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