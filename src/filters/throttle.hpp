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

#ifndef THROTTLE_HPP
#define THROTTLE_HPP

#include "filter.hpp"
#include "input.hpp"
#include "list.hpp"
#include "timer.hpp"

#include <memory>
#include <unordered_map>

namespace pipy {

//
// ThrottleBase
//

class ThrottleBase : public Filter, public List<ThrottleBase>::Item {
public:
  ThrottleBase(const pjs::Value &quota, const pjs::Value &account, bool auto_supply);

protected:
  ThrottleBase(const ThrottleBase &r);
  ~ThrottleBase();

  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual auto consume(Event *evt, double &quota) -> Event* = 0;

  class Account :
    public pjs::RefCount<Account>,
    public pjs::Pooled<Account>
  {
  public:
    void enqueue(ThrottleBase *filter);
    void dequeue(ThrottleBase *filter);
    void supply(double quota);

  private:
    List<ThrottleBase> m_queue;
    double m_quota;
    double m_quota_supply;

    void supply();

    friend class AccountManager;
    friend class ThrottleBase;
  };

  bool m_evaluated = false;
  pjs::Ref<Account> m_current_account;

private:
  class AccountManager {
  public:
    AccountManager(bool auto_supply);

    auto get(const pjs::Value &key, double quota) -> Account*;

  private:
    std::unordered_map<pjs::Value, pjs::Ref<Account>> m_accounts;
    std::unordered_map<pjs::WeakRef<pjs::Object>, pjs::Ref<Account>> m_weak_accounts;
    Timer m_timer;

    void supply();
  };

  std::shared_ptr<AccountManager> m_account_manager;
  pjs::Value m_quota;
  pjs::Value m_account;
  EventBuffer m_buffer;
  pjs::Ref<InputSource::Tap> m_closed_tap;

  void pause();
  void resume();
  bool flush();
};

//
// ThrottleMessageRate
//

class ThrottleMessageRate : public ThrottleBase {
public:
  ThrottleMessageRate(const pjs::Value &quota, const pjs::Value &account);

protected:
  ThrottleMessageRate(const ThrottleMessageRate &r);

  virtual auto clone() -> Filter* override;
  virtual auto consume(Event *evt, double &quota) -> Event* override;
  virtual void dump(std::ostream &out) override;
};

//
// ThrottleDataRate
//

class ThrottleDataRate : public ThrottleBase {
public:
  ThrottleDataRate(const pjs::Value &quota, const pjs::Value &account);

protected:
  ThrottleDataRate(const ThrottleDataRate &r);

  virtual auto clone() -> Filter* override;
  virtual auto consume(Event *evt, double &quota) -> Event* override;
  virtual void dump(std::ostream &out) override;
};

//
// ThrottleConcurrency
//

class ThrottleConcurrency : public ThrottleBase {
public:
  ThrottleConcurrency(const pjs::Value &quota, const pjs::Value &account);

protected:
  ThrottleConcurrency(const ThrottleConcurrency &r);

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual auto consume(Event *evt, double &quota) -> Event* override;
  virtual void dump(std::ostream &out) override;

private:
  bool m_active = false;
};

} // namespace pipy

#endif // THROTTLE_HPP
