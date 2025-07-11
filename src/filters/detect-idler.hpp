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

#ifndef DETECT_IDLER_HPP
#define DETECT_IDLER_HPP

#include "filter.hpp"
#include "timer.hpp"

namespace pipy {

//
// DetectIdler
//

class DetectIdler : public Filter, public EventSource, public Ticker::Watcher {
public:
  DetectIdler(double timeout, pjs::Function *on_idle);

private:
  DetectIdler(const DetectIdler &r);
  ~DetectIdler();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void on_tick(double tick) override;
  virtual void dump(Dump &d) override;

  double m_timeout;
  pjs::Ref<pjs::Function> m_on_idle;
  pjs::Ref<Pipeline> m_pipeline;
  double m_busy_time = 0;
  bool m_is_idle = false;
};

} // namespace pipy

#endif // DETECT_IDLER_HPP
