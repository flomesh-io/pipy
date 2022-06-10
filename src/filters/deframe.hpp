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

#ifndef DEFRAME_HPP
#define DEFRAME_HPP

#include "filter.hpp"
#include "deframer.hpp"

namespace pipy {

//
// Deframe
//

class Deframe : public Filter, public Deframer {
public:
  Deframe(pjs::Object *states);

private:
  Deframe(const Deframe &r);
  ~Deframe();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<pjs::Object> m_states;
  pjs::Ref<pjs::Object> m_state_map;
  pjs::Ref<pjs::Array> m_state_array;
  pjs::Ref<pjs::Object> m_read_buffer;

  virtual auto on_state(int state, int c) -> int override;
  virtual void on_pass(const Data &data) override;
};

} // namespace pipy

#endif // DEFRAME_HPP
