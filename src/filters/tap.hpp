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
#include "list.hpp"
#include "timer.hpp"

#include <set>
#include <unordered_map>
#include <chrono>

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

  class Channel : public pjs::Pooled<Channel> {
  public:
    Channel(Context *ctx, const Event::Receiver &out)
      : m_ctx(ctx)
      , m_out(out) {}

    void push(Event *inp);
    void drain();
    bool deduct_by_data(int &quota);
    bool deduct_by_message(int &quota);
    void pause();
    void resume();

  private:
    Context* m_ctx;
    Event::Receiver m_out;
    std::list<pjs::Ref<Event>> m_buffer;
    bool m_paused = false;
  };

  class Account : public pjs::Pooled<Account>, public List<Account>::Item {
  public:
    Account() {}
    Account(const std::string &name) : m_name(name) {}

    auto name() const -> const std::string& { return m_name; }
    void setup(int quota, bool is_data);
    auto setup_time() const -> double { return m_setup_time; }
    bool unlimited() const { return m_initial_quota < 0; }
    bool blocking() const { return !m_queue.empty(); }
    void queue(Channel *channel);
    void clear(Channel *channel);
    void supply();

  private:
    std::string m_name;
    std::list<Channel*> m_queue;
    std::set<Channel*> m_paused_channels;
    int m_initial_quota = 0;
    int m_current_quota = 0;
    bool m_is_set_up = false;
    bool m_is_data = false;
    double m_setup_time = 0;

    void pump();
  };

  class AccountManager {
  public:
    AccountManager() {
      supply();
    }

    auto get() -> Account* {
      auto account = new Account();
      m_accounts.push(account);
      return account;
    }

    auto get(const std::string &name) -> Account* {
      auto i = m_named_accounts.find(name);
      if (i != m_named_accounts.end()) return i->second;
      auto account = new Account(name);
      m_named_accounts[name] = account;
      m_accounts.push(account);
      return account;
    }

    void close(Account *account) {
      if (!account->name().empty()) {
        m_named_accounts.erase(account->name());
      }
      m_accounts.remove(account);
      delete account;
    }

  private:
    List<Account> m_accounts;
    std::unordered_map<std::string, Account*> m_named_accounts;
    Timer m_timer;

    void supply() {
      for (auto account = m_accounts.head(); account; account = account->next()) {
        account->supply();
      }
      m_timer.schedule(1.0, [this]() {
        supply();
      });
    }
  };

  std::shared_ptr<AccountManager> m_account_manager;
  pjs::Value m_quota;
  pjs::Value m_account;
  Account* m_session_account = nullptr;
  Account* m_current_account = nullptr;
  Channel* m_channel = nullptr;
  bool m_initialized = false;
  bool m_session_end = false;

  void set_quota(const pjs::Value &quota);
};

} // namespace pipy

#endif // TAP_HPP