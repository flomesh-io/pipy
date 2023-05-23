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

#ifndef LINK_SWAP_HPP
#define LINK_SWAP_HPP

#include "filter.hpp"
#include "api/swap.hpp"

namespace pipy {

//
// LinkSwap
//

class LinkSwap : public Filter, public EventSource {
public:
  LinkSwap(pjs::Object *swap);

private:
  LinkSwap(const LinkSwap &r);
  ~LinkSwap();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<Swap> m_swap;
  pjs::Ref<pjs::Function> m_swap_f;
  bool m_is_linked = false;
};

} // namespace pipy

#endif // LINK_SWAP_HPP
