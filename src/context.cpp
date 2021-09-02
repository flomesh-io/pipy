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

#include "context.hpp"
#include "worker.hpp"
#include "session.hpp"
#include "logging.hpp"

namespace pipy {

//
// Context
//

uint64_t Context::s_context_id = 0;
uint64_t Context::s_context_total = 0;

Context::Context()
  : Context(nullptr, nullptr, nullptr)
{
}

Context::Context(ContextGroup *group, Worker *worker, pjs::Object *global, ContextData *data)
  : pjs::Context(global, data ? data->elements() : nullptr)
  , m_group(group ? group : new ContextGroup())
  , m_worker(worker)
  , m_data(data)
{
  m_group->add(this);
  if (data) {
    for (size_t i = 0, n = data->size(); i < n; i++) {
      data->at(i)->as<ContextDataBase>()->m_context = this;
    }
  }
  if (!++s_context_id) s_context_id++;
  m_id = s_context_id;
  Log::debug("Context: %p, allocated, id = %llu, total = %llu", this, m_id, ++s_context_total);
}

Context::~Context() {
  m_group->remove(this);
  if (m_data) m_data->free();
  Log::debug("Context: %p, freed, id = %llu, total = %llu", this, m_id, --s_context_total);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<ContextDataBase>::init() {
  accessor("__filename", [](Object *obj, Value &ret) { ret.set(obj->as<ContextDataBase>()->filename()); });
  accessor("__inbound", [](Object *obj, Value &ret) { ret.set(obj->as<ContextDataBase>()->inbound()); });
}

} // namespace pjs