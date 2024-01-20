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

#include "net.hpp"

namespace pipy {

Net* Net::s_main = nullptr;
thread_local Net Net::s_current;

void Net::init() {
  s_main = &s_current;

#ifdef _WIN32
  asio::detail::win_thread::set_terminate_threads(true);
#endif
}

void Net::run() {
  m_is_running = true;
  m_io_context.run();
  m_is_running = false;
}

auto Net::run_one() -> size_t {
  m_is_running = true;
  auto n = m_io_context.run_one();
  m_is_running = false;
  return n;
}

void Net::stop() {
  m_io_context.stop();
}

void Net::restart() {
  m_io_context.restart();
}

void Net::post(const std::function<void()> &cb) {
  asio::post(m_io_context, cb);
}

void Net::defer(const std::function<void()> &cb) {
  asio::defer(m_io_context, cb);
}

} // namespace pipy
