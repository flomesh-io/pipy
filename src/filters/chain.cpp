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

#include "chain.hpp"
#include "module.hpp"

namespace pipy {

//
// Chain
//

Chain::Chain(const std::list<JSModule*> &modules)
  : m_modules(modules)
{
}

Chain::Chain(const Chain &r)
  : m_chain(r.m_chain)
{
}

Chain::~Chain()
{
}

void Chain::dump(Dump &d) {
  Filter::dump(d);
  std::string module_name;
  if (m_modules.size() > 0) {
    module_name = m_modules.front()->filename()->str();
  } else {
    module_name = "(0 modules)";
  }
  if (m_modules.size() > 1) {
    module_name += " (plus ";
    module_name += std::to_string(m_modules.size() - 1);
    module_name += " more)";
  }
  d.name = "chain [";
  d.name += module_name;
  d.name += ']';
}

void Chain::bind() {
  Filter::bind();
  PipelineLayout::Chain *chain = nullptr;
  for (auto *mod : m_modules) {
    auto p = mod->entrance_pipeline();
    if (!p) {
      std::string msg("entrance pipeline not found in module ");
      msg += mod->filename()->str();
      throw std::runtime_error(msg);
    }
    if (chain) {
      chain = chain->next = new PipelineLayout::Chain;
    } else {
      chain = new PipelineLayout::Chain;
      m_chain = chain;
    }
    chain->layout = p;
  }
}

auto Chain::clone() -> Filter* {
  return new Chain(*this);
}

void Chain::reset() {
  Filter::reset();
  m_entrance = nullptr;
}

void Chain::process(Event *evt) {
  if (!m_entrance && m_chain) {
    auto *p = Pipeline::make(m_chain->layout, context());
    p->chain(Filter::output());
    p->chain(m_chain->next);
    p->start();
    m_entrance = p;
  }

  if (m_entrance) {
    Filter::output(evt, m_entrance->input());
  } else {
    Filter::output(evt);
  }
}

//
// ChainNext
//

ChainNext::ChainNext()
{
}

ChainNext::ChainNext(const Chain &r)
{
}

ChainNext::~ChainNext()
{
}

void ChainNext::dump(Dump &d) {
  Filter::dump(d);
  d.name = "chain";
}

auto ChainNext::clone() -> Filter* {
  return new ChainNext(*this);
}

void ChainNext::reset() {
  Filter::reset();
  m_next = nullptr;
}

void ChainNext::process(Event *evt) {
  if (auto *chain = pipeline()->chain()) {
    if (!m_next) {
      auto *p = Pipeline::make(chain->layout, context());
      p->chain(Filter::output());
      p->chain(chain->next);
      p->start();
      m_next = p;
    }
  }

  if (m_next) {
    Filter::output(evt, m_next->input());
  } else {
    Filter::output(evt);
  }
}

} // namespace pipy
