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

#include "link-swap.hpp"

namespace pipy {

//
// LinkSwap
//

LinkSwap::LinkSwap(pjs::Object *swap) {
  if (swap && swap->is<Swap>()) {
    m_swap = swap->as<Swap>();
  } else if (swap && swap->is<pjs::Function>()) {
    m_swap_f = swap->as<pjs::Function>();
  } else {
    throw std::runtime_error("expects a Swap object or a function");
  }
}

LinkSwap::LinkSwap(const LinkSwap &r)
  : Filter(r)
  , m_swap(r.m_swap)
  , m_swap_f(r.m_swap_f)
{
}

LinkSwap::~LinkSwap()
{
}

void LinkSwap::dump(Dump &d) {
  Filter::dump(d);
  d.name = "swap";
}

auto LinkSwap::clone() -> Filter* {
  return new LinkSwap(*this);
}

void LinkSwap::reset() {
  Filter::reset();
  EventSource::close();
  if (m_swap_f) {
    m_swap = nullptr;
  }
  m_is_linked = false;
}

void LinkSwap::process(Event *evt) {
  if (!m_is_linked) {
    m_is_linked = true;
    if (m_swap_f) {
      pjs::Value arg(evt), ret;
      if (!Filter::callback(m_swap_f, 1, &arg, ret)) return;
      if (!ret.is_instance_of<Swap>()) {
        Filter::error("callback did not return a Swap object");
        return;
      }
      m_swap = ret.as<Swap>();
    }

    if (!m_swap->chain_output(EventSource::reply())) {
      Filter::error("Swap's output end occupied");
      m_swap = nullptr;
    }
  }

  if (m_swap) {
    m_swap->input(evt);
  }
}

void LinkSwap::on_reply(Event *evt) {
  Filter::output(evt);
}

} // namespace pipy
