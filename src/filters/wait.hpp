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

#ifndef WAIT_HPP
#define WAIT_HPP

#include "filter.hpp"
#include "context.hpp"
#include "timer.hpp"
#include "options.hpp"

#include <vector>

namespace pipy {

//
// Wait
//

class Wait : public Filter, public ContextGroup::Waiter {
public:
  struct Options : public pipy::Options {
    double timeout = 0;

    Options() {}
    Options(pjs::Object *options);
  };

  Wait(pjs::Function *condition, const Options &options);

private:
  Wait(const Wait &r);
  ~Wait();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  virtual void on_notify(Context *ctx) override;

  pjs::Ref<pjs::Function> m_condition;
  Options m_options;
  EventBuffer m_buffer;
  Timer m_timer;
  bool m_fulfilled = false;

  void fulfill();
};

} // namespace pipy

#endif // WAIT_HPP
