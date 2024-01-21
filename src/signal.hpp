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

#ifndef SIGNAL_HPP
#define SIGNAL_HPP

#include "pjs/pjs.hpp"
#include "net.hpp"

#include <functional>

namespace pipy {

//
// Signal
//

class Signal : public pjs::Pooled<Signal> {
public:
  Signal(const std::function<void()> &handler = nullptr);

  void fire();

private:

  //
  // Signal::Handler
  //

  class Handler :
    public pjs::RefCount<Handler>,
    public pjs::Pooled<Handler>
  {
  public:
    Handler(const std::function<void()> &handler)
      : m_handler(handler) {}

    void trigger();

  private:
    std::function<void()> m_handler;
  };

  asio::steady_timer m_timer;
  pjs::Ref<Handler> m_handler;
  bool m_fired = false;

  void wait();
};

} // namespace pipy

#endif // SIGNAL_HPP
