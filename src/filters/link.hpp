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

#ifndef LINK_HPP
#define LINK_HPP

#include "filter.hpp"

#include <vector>
#include <memory>
#include <utility>

namespace pipy {

//
// Link
//

class Link : public Filter {
public:
  Link();

  void add_condition(pjs::Function *func);
  void add_condition(const std::function<bool()> &func);

private:
  Link(const Link &r);
  ~Link();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  struct Condition {
    pjs::Ref<pjs::Function> func;
    std::function<bool()> cpp_func;
  };

  std::shared_ptr<std::vector<Condition>> m_conditions;
  pjs::Ref<Pipeline> m_pipeline;
  EventBuffer m_buffer;
  bool m_chosen = false;
};

} // namespace pipy

#endif // LINK_HPP
