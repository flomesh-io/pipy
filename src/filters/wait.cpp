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

#include "wait.hpp"
#include "pipeline.hpp"
#include "logging.hpp"

namespace pipy {

//
// Wait::Options
//

Wait::Options::Options(pjs::Object *options) {
  Value(options, "timeout")
    .get(timeout)
    .check_nullable();
}

//
// Wait
//

Wait::Wait(pjs::Function *condition, const Options &options)
  : m_condition(condition)
  , m_options(options)
{
}

Wait::Wait(const Wait &r)
  : Filter(r)
  , m_condition(r.m_condition)
  , m_options(r.m_options)
{
}

Wait::~Wait()
{
}

void Wait::dump(Dump &d) {
  Filter::dump(d);
  d.name = "wait";
}

auto Wait::clone() -> Filter* {
  return new Wait(*this);
}

void Wait::reset() {
  Filter::reset();
  Waiter::cancel();
  m_timer.cancel();
  m_buffer.clear();
  m_fulfilled = false;
}

void Wait::process(Event *evt) {
  if (m_fulfilled) {
    output(evt);

  } else {
    pjs::Value ret;
    if (!callback(m_condition, 0, nullptr, ret)) return;
    if (ret.to_boolean()) {
      fulfill();
      output(evt);
    } else {
      if (m_buffer.empty() && m_options.timeout > 0) {
        m_timer.schedule(
          m_options.timeout,
          [=]() { fulfill(); }
        );
      }
      Waiter::wait(context()->group());
      m_buffer.push(evt);
    }
  }
}

void Wait::on_notify(Context *ctx) {
  pjs::Value ret;
  if (!callback(m_condition, 0, nullptr, ret)) return;
  if (ret.to_boolean()) {
    fulfill();
  }
}

void Wait::fulfill() {
  if (!m_fulfilled) {
    Waiter::cancel();
    m_timer.cancel();
    m_fulfilled = true;
    m_buffer.flush(
      [this](Event *evt) {
        output(evt);
      }
    );
  }
}

} // namespace pipy
