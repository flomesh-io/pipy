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

#include "throttle.hpp"
#include "pipeline.hpp"
#include "utils.hpp"
#include "logging.hpp"

namespace pipy {

//
// ThrottleBase
//

ThrottleBase::ThrottleBase(const pjs::Value &quota, const pjs::Value &account, bool auto_supply)
  : m_account_manager(std::make_shared<AccountManager>(auto_supply))
  , m_quota(quota)
  , m_account(account)
{
}

ThrottleBase::ThrottleBase(const ThrottleBase &r)
  : Filter(r)
  , m_account_manager(r.m_account_manager)
  , m_quota(r.m_quota)
  , m_account(r.m_account)
{
}

ThrottleBase::~ThrottleBase()
{
}

void ThrottleBase::reset() {
  Filter::reset();
  resume();
  m_evaluated = false;
  m_current_account = nullptr;
  m_buffer.clear();
}

void ThrottleBase::process(Event *evt) {
  if (Data::is_flush(evt)) return;

  if (!m_evaluated) {
    pjs::Value account, quota;
    if (!eval(m_account, account)) return;
    if (!eval(m_quota, quota)) return;
    double quota_supply = 0;
    if (quota.is_number()) {
      quota_supply = quota.n();
    } else if (quota.is_string()) {
      quota_supply = utils::get_byte_size(quota.s()->str());
    } else {
      Log::error("[throttle] invalid quota");
    }
    if (quota_supply > 0) {
      m_current_account = m_account_manager->get(account, quota_supply);
    }
    m_evaluated = true;
  }

  if (!m_current_account) {
    output(evt);

  } else if (m_closed_tap) {
    m_buffer.push(evt);

  } else if (auto stalled = consume(evt, m_current_account->m_quota)) {
    pause();
    m_buffer.push(stalled);
  }
}

void ThrottleBase::pause() {
  if (!m_closed_tap) {
    m_closed_tap = InputContext::tap();
    m_closed_tap->close();
    m_current_account->enqueue(this);
  }
}

void ThrottleBase::resume() {
  if (m_closed_tap) {
    m_current_account->dequeue(this);
    m_closed_tap->open();
    m_closed_tap = nullptr;
  }
}

bool ThrottleBase::flush() {
  while (auto evt = m_buffer.shift()) {
    if (auto stalled = consume(evt, m_current_account->m_quota)) {
      m_buffer.unshift(stalled);
      evt->release();
      return false;
    } else {
      evt->release();
    }
  }
  resume();
  return true;
}

//
// ThrottleBase::Account
//

void ThrottleBase::Account::enqueue(ThrottleBase *filter) {
  m_queue.push(filter);
}

void ThrottleBase::Account::dequeue(ThrottleBase *filter) {
  m_queue.remove(filter);
}

void ThrottleBase::Account::supply(double quota) {
  m_quota += quota;
  if (m_quota >= 1) {
    auto *f = m_queue.head();
    while (f) {
      auto *filter = f;
      f = f->List<ThrottleBase>::Item::next();
      if (!filter->flush()) break;
    }
  }
}

void ThrottleBase::Account::supply() {
  if (m_quota < 1 || m_quota < m_quota_supply) {
    supply(m_quota_supply);
  }
}

//
// ThrottleBase::AccountManager
//

ThrottleBase::AccountManager::AccountManager(bool auto_supply) {
  if (auto_supply) {
    supply();
  }
}

ThrottleBase::AccountManager::~AccountManager() {
  for (const auto &p : m_accounts) {
    delete p.second;
  }
}

auto ThrottleBase::AccountManager::get(const pjs::Value &key, double quota) -> Account* {
  bool is_weak = (key.is_object() && key.o());
  Account* account = nullptr;

  if (is_weak) {
    pjs::WeakRef<pjs::Object> o(key.o());
    auto i = m_weak_accounts.find(o);
    if (i != m_weak_accounts.end()) {
      account = i->second;
      if (!i->first.ptr()) {
        account = nullptr;
      }
    }
  } else {
    auto i = m_accounts.find(key);
    if (i != m_accounts.end()) {
      account = i->second;
    }
  }

  if (!account) {
    account = new Account();
    account->m_quota = quota;
    if (is_weak) {
      m_weak_accounts[key.o()] = account;
    } else {
      m_accounts[key] = account;
    }
  }

  account->m_quota_supply = quota;
  return account;
}

void ThrottleBase::AccountManager::supply() {
  InputContext ic;
  for (const auto &p : m_accounts) p.second->supply();
  for (const auto &p : m_weak_accounts) p.second->supply();
  m_timer.schedule(
    1.0,
    [this]() {
      supply();
    }
  );
}

//
// ThrottleMessageRate
//

ThrottleMessageRate::ThrottleMessageRate(const pjs::Value &quota, const pjs::Value &account)
  : ThrottleBase(quota, account, true)
{
}

ThrottleMessageRate::ThrottleMessageRate(const ThrottleMessageRate &r)
  : ThrottleBase(r)
{
}

void ThrottleMessageRate::dump(std::ostream &out) {
  out << "throttleMessageRate";
}

auto ThrottleMessageRate::clone() -> Filter* {
  return new ThrottleMessageRate(*this);
}

auto ThrottleMessageRate::consume(Event *evt, double &quota) -> Event* {
  if (evt->is<MessageStart>()) {
    if (quota >= 1) {
      quota -= 1;
      output(evt);
      return nullptr;
    } else {
      return evt;
    }
  } else {
    output(evt);
    return nullptr;
  }
}

//
// ThrottleDataRate
//

ThrottleDataRate::ThrottleDataRate(const pjs::Value &quota, const pjs::Value &account)
  : ThrottleBase(quota, account, true)
{
}

ThrottleDataRate::ThrottleDataRate(const ThrottleDataRate &r)
  : ThrottleBase(r)
{
}

void ThrottleDataRate::dump(std::ostream &out) {
  out << "throttleDataRate";
}

auto ThrottleDataRate::clone() -> Filter* {
  return new ThrottleDataRate(*this);
}

auto ThrottleDataRate::consume(Event *evt, double &quota) -> Event* {
  if (auto data = evt->as<Data>()) {
    int n = int(quota);
    if (data->size() <= n) {
      quota -= data->size();
      output(data);
      return nullptr;
    } else {
      auto partial = Data::make();
      data->shift(n, *partial);
      quota = 0;
      output(partial);
      return data;
    }
  } else {
    output(evt);
    return nullptr;
  }
}

//
// ThrottleConcurrency
//

ThrottleConcurrency::ThrottleConcurrency(const ThrottleConcurrency &r)
  : ThrottleBase(r)
{
}

ThrottleConcurrency::ThrottleConcurrency(const pjs::Value &quota, const pjs::Value &account)
  : ThrottleBase(quota, account, false)
{
}

void ThrottleConcurrency::dump(std::ostream &out) {
  out << "throttleConcurrency";
}

auto ThrottleConcurrency::clone() -> Filter* {
  return new ThrottleConcurrency(*this);
}

void ThrottleConcurrency::reset() {
  if (m_active) {
    if (m_current_account) {
      m_current_account->supply(1);
    }
    m_active = false;
  }
  ThrottleBase::reset();
}

auto ThrottleConcurrency::consume(Event *evt, double &quota) -> Event* {
  if (m_active) {
    output(evt);
    return nullptr;
  }

  if (quota < 1) {
    return evt;
  }

  quota -= 1;
  m_active = true;

  output(evt);
  return nullptr;
}

} // namespace pipy
