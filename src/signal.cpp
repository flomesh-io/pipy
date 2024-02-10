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

#include "signal.hpp"
#include "input.hpp"

namespace pipy {

//
// Signal
//

Signal::Signal(const std::function<void()> &handler)
  : m_timer(Net::current().context())
  , m_handler(new Handler(handler))
{
  wait();
}

Signal::~Signal() {
  m_handler->m_closed = true;
}

void Signal::wait() {
  auto handler = m_handler.get();
  handler->retain();
  m_timer.expires_after(std::chrono::minutes(1));
  m_timer.async_wait(
    [=](const asio::error_code &) {
      if (!handler->m_closed) {
        if (handler->m_fired) {
          if (handler->m_handler) {
            InputContext ic;
            handler->m_handler();
          }
        } else {
          wait();
        }
      }
      handler->release();
    }
  );
}

void Signal::fire() {
  m_handler->m_fired = true;
  m_timer.cancel();
}

} // namespace pipy
