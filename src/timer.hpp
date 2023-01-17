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

} // namespace pipy

#endif // TIMER_HPP
