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

#ifndef TAP_HPP
#define TAP_HPP

#include "module.hpp"

#include <chrono>
#include <list>

NS_BEGIN

//
// Tap
//

class Tap : public Module {
public:
  Tap();

private:
  Tap(const Tap &other);
  ~Tap();

  virtual auto help() -> std::list<std::string> override;
  virtual void config(const std::map<std::string, std::string> &params) override;
  virtual auto clone() -> Module* override;
  virtual void pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) override;

  class SharedControl {
  public:
    void config(int limit);
    bool request_quota(std::function<void()> drainer);

  private:
    std::list<std::function<void()>> m_drainers;
    std::chrono::time_point<std::chrono::steady_clock> m_window_start;
    int m_limit = 100;
    int m_quota = 100;
    bool m_is_draining = false;

    void drain();
  };

  std::shared_ptr<SharedControl> m_shared_control;
  std::list<std::unique_ptr<Object>> m_buffer;
  uint64_t m_context_id = 0;
  bool m_is_blocking = false;

  void drain(Object::Receiver out);
};

NS_END

#endif // TAP_HPP

