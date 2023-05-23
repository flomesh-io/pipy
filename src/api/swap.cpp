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

#include "api/swap.hpp"

namespace pipy {

void Swap::input(Event *evt) {
  if (m_is_input_chained) {
    EventProxy::forward(evt);
  } else if (evt->is<StreamEnd>()) {
    EventProxy::output(evt);
  }
}

void Swap::output(Event *evt) {
  if (m_is_output_chained) {
    EventProxy::output(evt);
  } else if (evt->is<StreamEnd>()) {
    EventProxy::forward(evt);
  }
}

bool Swap::chain_input(EventTarget::Input *input) {
  if (m_is_input_chained) return false;
  chain_forward(input);
  m_is_input_chained = true;
  return true;
}

bool Swap::chain_output(EventTarget::Input *input) {
  if (m_is_output_chained) return false;
  chain(input);
  m_is_output_chained = true;
  return true;
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Swap>::init() {
  ctor();
}

template<> void ClassDef<Constructor<Swap>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
