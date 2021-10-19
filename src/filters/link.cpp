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
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "logging.hpp"

namespace pipy {

//
// Link
//

Link::Link()
{
}

Link::Link(std::list<Route> &&routes)
  : m_routes(std::make_shared<std::list<Route>>(std::move(routes)))
{
}

Link::Link(const Link &r)
  : m_routes(r.m_routes)
{
}

Link::~Link()
{
}

auto Link::help() -> std::list<std::string> {
  return {
    "link(target[, when[, target2[, when2, ...]]])",
    "Sends events to a different pipeline",
    "target = <string> Name of the pipeline to send events to",
    "when = <function> Callback function that returns true if a target should be chosen",
  };
}

void Link::dump(std::ostream &out) {
  out << "link";
}

auto Link::draw(std::list<std::string> &links, bool &fork) -> std::string {
  for (const auto &r : *m_routes) {
    if (r.name) {
      links.push_back(r.name->str());
    } else {
      links.push_back("");
    }
  }
  fork = false;
  return "link";
}

void Link::bind() {
  auto mod = pipeline()->module();
  for (auto &r : *m_routes) {
    if (!r.pipeline && r.name) {
      r.pipeline = pipeline(r.name);
    }
  }
}

auto Link::clone() -> Filter* {
  return new Link(*this);
}

void Link::reset() {
  m_session = nullptr;
  m_buffer.clear();
  m_chosen = false;
  m_session_end = false;
}

void Link::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (!m_chosen) {
    if (auto data = inp->as<Data>()) {
      if (data->empty()) {
        return;
      }
    }
    for (const auto &r : *m_routes) {
      if (r.condition) {
        pjs::Value ret;
        if (!callback(*ctx, r.condition, 0, nullptr, ret)) return;
        m_chosen = ret.to_boolean();
      } else {
        m_chosen = true;
      }
      if (m_chosen) {
        if (r.pipeline) {
          auto root = static_cast<Context*>(ctx->root());
          auto session = Session::make(root, r.pipeline);
          session->on_output(out());
          m_session = session;
          m_buffer.flush([=](Event *inp) { session->input(inp); });
        }
        break;
      }
    }
  }

  if (!m_chosen) {
    m_buffer.push(inp);
  } if (m_session) {
    m_session->input(inp);
  } else {
    output(inp);
  }

  if (inp->is<SessionEnd>()) m_session_end = true;
}

} // namespace pipy