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

#include "throttle.hpp"
#include "pipeline.hpp"
#include "utils.hpp"
#include "log.hpp"

namespace pipy {

//
// ThrottleBase
//

ThrottleBase::ThrottleBase(pjs::Object *quota) {
  if (quota) {
    if (quota->is<algo::Quota>()) {
      m_quota = quota->as<algo::Quota>();
      return;
    } else if (quota->is<pjs::Function>()) {
      m_quota_f = quota->as<pjs::Function>();
      return;
    }
  }
  throw std::runtime_error(
    "throttle filter expects an algo.Quota or a function that returns that"
  );
}

ThrottleBase::ThrottleBase(const ThrottleBase &r)
  : Filter(r)
  , m_quota(r.m_quota)
  , m_quota_f(r.m_quota_f)
{
}

ThrottleBase::~ThrottleBase()
{
}

void ThrottleBase::reset() {
  Filter::reset();
  resume();
  if (m_quota_f) m_quota = nullptr;
  m_buffer.clear();
}

void ThrottleBase::process(Event *evt) {
  if (Data::is_flush(evt)) return;

  if (!m_quota) {
    pjs::Value ret;
    if (!eval(m_quota_f, ret)) return;
    if (!ret.is<algo::Quota>()) {
      Log::error("[throttle] function did not return an object of type algo.Quota");
      return;
    }
    m_quota = ret.as<algo::Quota>();
  }

  if (m_closed_tap) {
    m_buffer.push(evt);

  } else if (m_quota) {
    if (auto stalled = consume(evt, m_quota)) {
      pause();
      m_buffer.push(stalled);
    }
  }
}

void ThrottleBase::pause() {
  if (!m_closed_tap) {
    m_closed_tap = InputContext::tap();
    m_closed_tap->close();
    if (m_quota) m_quota->enqueue(this);
  }
}

void ThrottleBase::resume() {
  if (m_closed_tap) {
    if (m_quota) m_quota->dequeue(this);
    m_closed_tap->open();
    m_closed_tap = nullptr;
  }
}

void ThrottleBase::on_consume(algo::Quota *quota) {
  while (auto evt = m_buffer.shift()) {
    if (auto stalled = consume(evt, quota)) {
      m_buffer.unshift(stalled);
      evt->release();
      return;
    } else {
      evt->release();
    }
  }
  resume();
}

//
// ThrottleMessageRate
//

void ThrottleMessageRate::dump(Dump &d) {
  Filter::dump(d);
  d.name = "throttleMessageRate";
}

auto ThrottleMessageRate::clone() -> Filter* {
  return new ThrottleMessageRate(*this);
}

auto ThrottleMessageRate::consume(Event *evt, algo::Quota *quota) -> Event* {
  if (evt->is<MessageStart>()) {
    if (quota->consume(1) > 0) {
      output(evt);
      return nullptr;
    } else {
      return evt;
    }
  } else {
    output(evt);
    return nullptr;
  }
}

//
// ThrottleDataRate
//

void ThrottleDataRate::dump(Dump &d) {
  Filter::dump(d);
  d.name = "throttleDataRate";
}

auto ThrottleDataRate::clone() -> Filter* {
  return new ThrottleDataRate(*this);
}

auto ThrottleDataRate::consume(Event *evt, algo::Quota *quota) -> Event* {
  if (auto data = evt->as<Data>()) {
    auto n = quota->consume(data->size());
    if (n == data->size()) {
      output(data);
      return nullptr;
    } else {
      auto partial = Data::make();
      data->shift(n, *partial);
      quota = 0;
      output(partial);
      return data;
    }
  } else {
    output(evt);
    return nullptr;
  }
}

//
// ThrottleConcurrency
//

void ThrottleConcurrency::dump(Dump &d) {
  Filter::dump(d);
  d.name = "throttleConcurrency";
}

auto ThrottleConcurrency::clone() -> Filter* {
  return new ThrottleConcurrency(*this);
}

void ThrottleConcurrency::reset() {
  if (m_active) {
    if (m_quota) m_quota->produce(1);
    m_active = false;
  }
  ThrottleBase::reset();
}

auto ThrottleConcurrency::consume(Event *evt, algo::Quota *quota) -> Event* {
  if (m_active) {
    output(evt);
    return nullptr;
  }

  if (quota->consume(1) == 0) {
    return evt;
  }

  m_active = true;

  output(evt);
  return nullptr;
}

} // namespace pipy
