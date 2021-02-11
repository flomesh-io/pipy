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

#include "clone.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "logging.hpp"
#include "utils.hpp"

NS_BEGIN

//
// Clone
//

Clone::Clone() {
}

Clone::~Clone() {
}

auto Clone::help() -> std::list<std::string> {
  return {
    "Makes a copy of stream to other pipelines",
    "to = Name of the target pipeline",
    "session = If specified, the name of session to share with others",
  };
}

void Clone::config(const std::map<std::string, std::string> &params) {
  m_to = utils::get_param(params, "to");
  m_session_name = utils::get_param(params, "session", "");
  m_pool = m_session_name.empty() ? nullptr : std::make_shared<Pool>();
}

auto Clone::clone() -> Module* {
  auto clone = new Clone();
  clone->m_to = m_to;
  clone->m_session_name = m_session_name;
  clone->m_pool = m_pool;
  return clone;
}

void Clone::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {

  if (m_pool) {
    if (obj->is<SessionStart>()) {
      auto name = ctx->evaluate(m_session_name);
      m_target = m_pool->get(name);
    } else if (obj->is<SessionEnd>()) {
      m_target = nullptr;
    } else if (obj->is<MessageStart>()) {
      m_buffer.clear();
      m_buffering = true;
    } else if (obj->is<MessageEnd>()) {
      if (m_target) {
        m_target->open(m_to);
        m_target->input(make_object<MessageStart>());
        for (auto &obj : m_buffer) m_target->input(std::move(obj));
        m_target->input(make_object<MessageEnd>());
      }
      m_buffer.clear();
      m_buffering = false;
    } else if (m_buffering) {
      m_buffer.push_back(clone_object(obj));
    }
  
  } else {
    if (!m_target) m_target = std::make_shared<Target>();
    if (obj->is<SessionStart>()) {
      m_target->close();
      m_target->open(m_to, ctx);
    } else if (obj->is<SessionEnd>()) {
      m_target->close();
    } else {
      m_target->input(clone_object(obj));
    }
  }

  out(std::move(obj));
}

//
// Clone::Pool
//

auto Clone::Pool::get(const std::string &name) -> std::shared_ptr<Target> {
  auto p = m_targets.find(name);
  if (p != m_targets.end()) return p->second;
  return m_targets[name] = std::make_shared<Target>();
}

//
// Clone::Target
//

void Clone::Target::open(const std::string &address, std::shared_ptr<Context> ctx) {
  if (m_session) return;
  if (auto pipeline = Pipeline::get(address)) {
    m_session = pipeline->alloc(ctx);
    m_session->input(make_object<SessionStart>());
  } else {
    Log::error("[clone] unknown pipeline: %s", address.c_str());
  }
}

void Clone::Target::input(std::unique_ptr<Object> obj) {
  if (m_session) {
    m_session->input(std::move(obj));
  }
}

void Clone::Target::close() {
  if (!m_session) return;
  m_session->input(make_object<SessionEnd>());
  m_session->free();
  m_session = nullptr;
}

NS_END
