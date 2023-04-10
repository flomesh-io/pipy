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

#include "timeout.hpp"
#include "input.hpp"

namespace pipy {

Timeout::Timeout(double t) : m_duration(t) {
  schedule();
}

Timeout::~Timeout() {
  while (auto w = m_waiters.head()) {
    m_waiters.remove(w);
    delete w;
  }
}

auto Timeout::wait() -> pjs::Promise* {
  if (m_state == TIMEOUT) {
    return pjs::Promise::resolve(this);
  } else if (m_state == CANCELED) {
    return pjs::Promise::reject(this);
  } else {
    auto promise = pjs::Promise::make();
    auto handler = pjs::Promise::Handler::make(promise);
    auto w = new Waiter;
    w->handler = handler;
    m_waiters.push(w);
    return promise;
  }
}

void Timeout::restart() {
  m_state = PENDING;
  schedule();
}

void Timeout::restart(double t) {
  m_duration = t;
  restart();
}

void Timeout::cancel() {
  m_state = CANCELED;
  m_timer.cancel();
  notify_waiters();
  if (m_scheduled) {
    m_scheduled = false;
    release();
  }
}

void Timeout::schedule() {
  m_timer.schedule(m_duration, [=]() { on_timeout(); });
  if (!m_scheduled) {
    retain();
    m_scheduled = true;
  }
}

void Timeout::on_timeout() {
  m_state = TIMEOUT;
  notify_waiters();
  if (m_scheduled) {
    m_scheduled = false;
    release();
  }
}

void Timeout::notify_waiters() {
  if (!m_waiters.empty()) {
    InputContext ic;
    pjs::Value v(this);
    while (auto w = m_waiters.head()) {
      m_waiters.remove(w);
      if (m_state == TIMEOUT) {
        w->handler->resolve(v);
      } else {
        w->handler->reject(v);
      }
      delete w;
    }
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Timeout>::init() {
  ctor(
    [](Context &ctx) -> Object* {
      double duration;
      if (!ctx.arguments(1, &duration)) return nullptr;
      return Timeout::make(duration);
    }
  );

  method("wait", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Timeout>()->wait());
  });

  method("restart", [](Context &ctx, Object *obj, Value &) {
    double duration;
    if (ctx.argc() > 0) {
      obj->as<Timeout>()->restart();
    } else if (ctx.arguments(1, &duration)) {
      obj->as<Timeout>()->restart(duration);
    }
  });

  method("cancel", [](Context &ctx, Object *obj, Value &) {
    obj->as<Timeout>()->cancel();
  });
}

template<> void ClassDef<Constructor<Timeout>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
