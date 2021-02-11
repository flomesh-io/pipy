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

#include "session.hpp"
#include "pipeline.hpp"

NS_BEGIN

Session::Session(
  Pipeline *pipeline,
  const std::list<std::unique_ptr<Module>> &chain
) : m_pipeline(pipeline)
{
  m_chain = new Chain {
    nullptr, nullptr,
    [this](std::unique_ptr<Object> obj) {
      if (m_output) m_output(std::move(obj));
    }
  };
  for (auto i = chain.rbegin(); i != chain.rend(); ++i) {
    auto module = (*i)->clone();
    auto output = m_chain->output;
    m_chain = new Chain {
      m_chain, module,
      [=](std::unique_ptr<Object> obj) {
        module->pipe(m_context, std::move(obj), output);
      }
    };
  }
}

Session::~Session() {
  for (auto p = m_chain; p; ) {
    auto next = p->next;
    delete p->module;
    delete p;
    p = next;
  }
}

void Session::input(std::unique_ptr<Object> obj) {
  m_chain->output(std::move(obj));
}

void Session::free() {
  m_pipeline->free(this);
}

NS_END