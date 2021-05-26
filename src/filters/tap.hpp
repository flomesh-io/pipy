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

#include "filter.hpp"
#include "data.hpp"
#include "timer.hpp"

#include <unordered_map>

namespace pipy {

//
// Tap
//

class Tap : public Filter {
public:
  Tap();
  Tap(const pjs::Value &quota, const pjs::Value &account);

private:
  Tap(const Tap &r);
  ~Tap();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  class Queue {
  public:
    void setup(int quota, bool is_data);
    void push(Context *ctx, Event *e, Event::Receiver out);
    bool blocking() const { return !m_queue.empty(); }

  private:
    struct Item {
      pjs::Ref<Event> event;
      Event::Receiver out;
    };

    std::list<Item> m_queue;
    std::set<pjs::Ref<Context>> m_paused_contexts;
    int m_initial_quota = 0;
    int m_current_quota = 0;
    bool m_is_data = false;
    bool m_has_set = false;
    bool m_blocking = false;
    bool m_supplying = false;
    Timer m_timer;

    auto deduct_data(Event *e) -> Data*;
    void deduct_message(Event *e);
    void pump();
    void supply();
  };

  class Accounts {
  public:
    auto get(const std::string &name) -> Queue* {
      auto i = m_accounts.find(name);
      if (i != m_accounts.end()) return i->second.get();
      auto q = new Queue();
      m_accounts[name] = std::unique_ptr<Queue>(q);
      return q;
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Queue>> m_accounts;
  };

  std::shared_ptr<Accounts> m_accounts;
  pjs::Value m_quota;
  pjs::Value m_account;
  Queue* m_queue = nullptr;
  bool m_initialized = false;
  bool m_session_end = false;
};

} // namespace pipy

#endif // TAP_HPP