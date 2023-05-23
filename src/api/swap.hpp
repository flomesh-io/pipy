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

#ifndef API_SWAP_HPP
#define API_SWAP_HPP

#include "event.hpp"

namespace pipy {

//
// Swap
//

class Swap : public pjs::ObjectTemplate<Swap>, protected EventProxy {
public:
  auto input() -> EventTarget::Input* { return EventProxy::forward(); }
  auto output() -> EventTarget::Input* { return EventProxy::output(); }
  void input(Event *evt);
  void output(Event *evt);
  bool chain_input(EventTarget::Input *input);
  bool chain_output(EventTarget::Input *input);

private:
  Swap() {}
  ~Swap() {}

  bool m_is_input_chained = false;
  bool m_is_output_chained = false;

  friend class pjs::ObjectTemplate<Swap>;
};

} // namespace pipy

#endif // API_SWAP_HPP
