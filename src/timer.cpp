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

#include "timer.hpp"
#include "input.hpp"

namespace pipy {

thread_local List<Timer> Timer::s_all_timers;

void Timer::cancel_all() {
  for (auto *timer = s_all_timers.head(); timer; timer = timer->next()) {
    timer->cancel();
  }
}

void Timer::schedule(double timeout, const std::function<void()> &handler) {
  cancel();
  pjs::Ref<Handler> h = new Handler(handler);
  m_handler = h;
  m_timer.expires_after(std::chrono::milliseconds((long long)(timeout * 1000)));
  m_timer.async_wait(
    [=](const asio::error_code &ec) {
      InputContext ic;
      h->trigger(ec);
    }
  );
}

void Timer::cancel() {
  if (m_handler) {
    asio::error_code ec;
    m_timer.cancel(ec);
    m_handler->cancel();
    m_handler = nullptr;
  }
}

void Timer::Handler::trigger(const asio::error_code &ec) {
  if (!m_canceled && ec != asio::error::operation_aborted) {
    m_handler();
  }
}

void Timer::Handler::cancel() {
  m_canceled = true;
}

//
// Ticker
//

auto Ticker::get() -> Ticker* {
  thread_local static Ticker s_ticker;
  return &s_ticker;
}

void Ticker::start() {
  if (!m_is_running) {
    schedule();
    m_is_running = true;
  }
}

void Ticker::stop() {
  if (m_is_running) {
    m_timer.cancel();
    m_is_running = false;
  }
}

void Ticker::schedule() {
  m_timer.schedule(
    1, [this]() {
      auto t = ++m_tick;
      m_visiting = m_watchers.head();
      while (auto w = m_visiting) {
        m_visiting = m_visiting->next();
        w->on_tick(t);
      }
      schedule();
    }
  );
}

} // namespace pipy
