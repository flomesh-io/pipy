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
    "throttle filter expects an algo.Quota or a function returning that"
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
  for (auto p = m_consumers.head(); p;) {
    auto c = p; p = p->next();
    delete c;
  }
  m_consumers.clear();
  if (m_quota_f) {
    m_quota = nullptr;
  }
}

void ThrottleBase::process(Event *evt) {
  if (Data::is_flush(evt)) return;

  if (!m_quota) {
    pjs::Value ret;
    if (!eval(m_quota_f, ret)) return;
    if (!ret.is<algo::Quota>()) {
      Filter::error("function did not return an object of type algo.Quota");
      return;
    }
    m_quota = ret.as<algo::Quota>();
  }

  if (m_paused) {
    enqueue(evt);

  } else if (auto stalled = consume(evt, m_quota)) {
    pause();
    enqueue(stalled);
  }
}

void ThrottleBase::pause() {
  if (!m_paused) {
    if (auto tap = InputContext::tap()) {
      tap->close();
      m_closed_tap = tap;
    }
    m_paused = true;
  }
}

void ThrottleBase::resume() {
  if (m_paused) {
    if (auto tap = m_closed_tap.get()) {
      tap->open();
      m_closed_tap = nullptr;
    }
    m_paused = false;
  }
}

void ThrottleBase::enqueue(Event *evt) {
  auto c = new EventConsumer(this, evt);
  m_consumers.push(c);
  m_quota->enqueue(c);
}

void ThrottleBase::dequeue(EventConsumer *consumer) {
  m_consumers.remove(consumer);
  if (m_consumers.empty()) {
    resume();
  }
  delete consumer;
}

//
// ThrottleBase::EventConsumer
//

bool ThrottleBase::EventConsumer::on_consume(algo::Quota *quota) {
  auto *t = m_throttle;
  if (auto stalled = t->consume(m_event, quota)) {
    m_event = stalled;
    return false;
  } else {
    t->dequeue(this);
    return true;
  }
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
      Filter::output(evt);
      return nullptr;
    } else {
      return evt;
    }
  } else {
    Filter::output(evt);
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
    int n = quota->consume(data->size());
    if (n == data->size()) {
      Filter::output(data);
      return nullptr;
    } else {
      auto partial = Data::make();
      data->pop(data->size() - n, *partial);
      Filter::output(data);
      return partial;
    }
  } else {
    Filter::output(evt);
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
