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

#ifndef TIMER_HPP
#define TIMER_HPP

#include "net.hpp"
#include "list.hpp"

namespace pipy {

//
// Timer
//

class Timer : public List<Timer>::Item {
public:
  static void cancel_all();

  Timer() : m_timer(Net::context()) {
    s_all_timers.push(this);
  }

  ~Timer() {
    s_all_timers.remove(this);
    cancel();
  }

  bool is_scheduled() const { return m_handler; }

  void schedule(double timeout, const std::function<void()> &handler);
  void cancel();

private:
  class Handler :
    public pjs::RefCount<Handler>,
    public pjs::Pooled<Handler>
  {
  public:
    Handler(const std::function<void()> &handler)
      : m_handler(handler) {}

    void trigger(const asio::error_code &ec);
    void cancel();

  private:
    std::function<void()> m_handler;
    bool m_canceled = false;
  };

  asio::steady_timer m_timer;
  pjs::Ref<Handler> m_handler;

  thread_local static List<Timer> s_all_timers;
};

//
// Ticker
//

class Ticker {
public:

  //
  // Ticker::Watcher
  //

  class Watcher : public List<Watcher>::Item {
    Ticker* m_ticker = nullptr;
    virtual void on_tick(double tick) = 0;
    friend class Ticker;
  };

  static auto get() -> Ticker*;

  double tick() const {
    return m_tick;
  }

  void watch(Watcher *w) {
    if (!w->m_ticker) {
      m_watchers.push(w);
      w->m_ticker = this;
      start();
    }
  }

  void unwatch(Watcher *w) {
    if (w->m_ticker == this) {
      if (w == m_visiting) {
        m_visiting = w->next();
      }
      m_watchers.remove(w);
      if (m_watchers.empty()) {
        stop();
      }
    }
  }

private:
  List<Watcher> m_watchers;
  Timer m_timer;
  Watcher* m_visiting = nullptr;
  double m_tick = 0;
  bool m_is_running = false;

  void start();
  void stop();
  void schedule();
};

} // namespace pipy

#endif // TIMER_HPP
