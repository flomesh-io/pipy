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
#include "filter.hpp"
#include "session.hpp"
#include "logging.hpp"

namespace pipy {

std::set<Pipeline*> Pipeline::s_all_pipelines;

Pipeline::Pipeline(Module *module, Type type, const std::string &name)
  : m_module(module)
  , m_type(type)
  , m_name(name)
{
  s_all_pipelines.insert(this);
}

Pipeline::~Pipeline() {
  auto *ptr = m_pool;
  while (ptr) {
    auto *session = ptr;
    ptr = ptr->m_next;
    delete session;
  }
  s_all_pipelines.erase(this);
}

void Pipeline::append(Filter *filter) {
  filter->m_pipeline = this;
  m_filters.emplace_back(filter);
}

auto Pipeline::alloc() -> ReusableSession* {
  retain();
  ReusableSession *session = nullptr;
  if (m_pool) {
    session = m_pool;
    m_pool = session->m_next;
  } else {
    session = new ReusableSession(this);
    m_allocated++;
  }
  m_active++;
  Log::debug("Session: %p, allocated, pipeline: %s", session, m_name.c_str());
  return session;
}

void Pipeline::free(ReusableSession *session) {
  session->m_next = m_pool;
  m_pool = session;
  m_active--;
  Log::debug("Session: %p, freed, pipeline: %s", session, m_name.c_str());
  release();
}

} // namespace pipy