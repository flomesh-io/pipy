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

#ifndef FORK_HPP
#define FORK_HPP

#include "filter.hpp"

namespace pipy {

//
// Fork
//

class Fork : public Filter {
public:
  enum Mode {
    FORK,
    JOIN,
    RACE,
  };

  Fork();
  Fork(const pjs::Value &init_arg);
  Fork(Mode mode, const pjs::Value &init_arg);

private:
  Fork(const Fork &r);
  ~Fork();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  struct Branch : public EventTarget {
    Fork *fork;
    pjs::Ref<Pipeline> pipeline;
    virtual void on_event(Event *evt) override;
  };

  Mode m_mode;
  pjs::Value m_init_arg;
  pjs::PooledArray<Branch>* m_branches = nullptr;
  Branch* m_winner = nullptr;
  EventBuffer m_buffer;
  int m_counter = 0;
  bool m_waiting = false;

  void on_branch_output(Branch *branch, Event *evt);
};

} // namespace pipy

#endif // FORK_HPP
