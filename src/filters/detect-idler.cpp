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

#include "detect-idler.hpp"

namespace pipy {

//
// DetectIdler
//

DetectIdler::DetectIdler(double timeout, pjs::Function *on_idle)
  : m_timeout(timeout)
  , m_on_idle(on_idle)
{
}

DetectIdler::DetectIdler(const DetectIdler &r)
  : Filter(r)
  , m_timeout(r.m_timeout)
  , m_on_idle(r.m_on_idle)
{
}

DetectIdler::~DetectIdler()
{
}

void DetectIdler::dump(Dump &d) {
  Filter::dump(d);
  d.name = "detectIdler";
}

auto DetectIdler::clone() -> Filter* {
  return new DetectIdler(*this);
}

void DetectIdler::reset() {
  Filter::reset();
  Ticker::get()->unwatch(this);
  m_pipeline = nullptr;
  m_is_idle = false;
}

void DetectIdler::process(Event *evt) {
  auto ticker = Ticker::get();

  if (!m_pipeline) {
    auto p = sub_pipeline(0, false);
    p->chain(EventSource::reply());
    p->start();
    m_pipeline = p;
    ticker->watch(this);
  }

  if (m_pipeline) {
    m_busy_time = ticker->tick();
    m_pipeline->input()->input(evt);
  }
}

void DetectIdler::on_reply(Event *evt) {
  auto ticker = Ticker::get();
  m_busy_time = ticker->tick();
  Filter::output(evt);
}

void DetectIdler::on_tick(double tick) {
  if (!m_is_idle && tick - m_busy_time >= m_timeout) {
    m_is_idle = true;
    pjs::Value ret;
    if (Filter::callback(m_on_idle, 0, nullptr, ret)) {
      if (!ret.is_nullish() && ret.is_object()) {
        Filter::output(ret.o());
      }
    }
  }
}

} // namespace pipy
