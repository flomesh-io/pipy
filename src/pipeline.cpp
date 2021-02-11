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

#include "pipeline.hpp"
#include "session.hpp"
#include "logging.hpp"

NS_BEGIN

std::map<std::string, Pipeline*> Pipeline::s_named_pipelines;
size_t Pipeline::s_session_total = 0;

auto Pipeline::get(const std::string &name) -> Pipeline* {
  auto p = s_named_pipelines.find(name);
  if (p == s_named_pipelines.end()) return nullptr;
  return p->second;
}

void Pipeline::add(const std::string &name, Pipeline *pipeline) {
  s_named_pipelines[name] = pipeline;
}

void Pipeline::remove(const std::string &name) {
  auto p = s_named_pipelines.find(name);
  if (p == s_named_pipelines.end()) return;
  auto pipeline = p->second;
  s_named_pipelines.erase(p);
  pipeline->m_removing = true;
  pipeline->clean();
}

Pipeline::Pipeline(std::list<std::unique_ptr<Module>> &chain)
  : m_chain(std::move(chain))
{
}

Pipeline::~Pipeline() {
  while (!m_pool.empty()) {
    delete m_pool.back();
    m_pool.pop_back();
  }
}

void Pipeline::update(std::list<std::unique_ptr<Module>> &chain) {
  m_chain = std::move(chain);
  while (!m_pool.empty()) {
    delete m_pool.back();
    m_pool.pop_back();
  }
  m_version++;
}

auto Pipeline::alloc(std::shared_ptr<Context> ctx) -> Session* {
  Session *session = nullptr;
  if (m_pool.empty()) {
    session = new Session(this, m_chain);
    session->m_version = m_version;
    m_allocated++;
  } else {
    session = m_pool.back();
    m_pool.pop_back();
  }
  session->m_context = ctx ? ctx : std::make_shared<Context>();
  s_session_total++;
  Log::debug("Session: %p, allocated", session);
  return session;
}

void Pipeline::free(Session *session) {
  if (session->m_version == m_version) {
    m_pool.push_back(session);
  } else {
    delete session;
    m_allocated--;
  }
  s_session_total--;
  Log::debug("Session: %p, freed", session);
  clean();
}

void Pipeline::clean() {
  if (m_removing && m_pool.size() == m_allocated) {
    delete this;
  }
}

NS_END