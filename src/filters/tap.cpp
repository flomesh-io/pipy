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

#include "tap.hpp"
#include "listener.hpp"
#include "context.hpp"
#include "inbound.hpp"
#include "utils.hpp"

namespace pipy {

//
// Tap
//

Tap::Tap()
{
}

Tap::Tap(const pjs::Value &quota, const pjs::Value &account)
  : m_account_manager(std::make_shared<AccountManager>())
  , m_quota(quota)
  , m_account(account)
{
}

Tap::Tap(const Tap &r)
  : m_account_manager(r.m_account_manager)
  , m_quota(r.m_quota)
  , m_account(r.m_account)
{
}

Tap::~Tap()
{
}

auto Tap::help() -> std::list<std::string> {
  return {
    "tap(quota[, account])",
    "Throttles message rate or data rate",
    "quota = <number|string|function> Quota in messages/sec when it is a number or in bytes/sec when string",
    "account = <string|function> Name under which the quota is entitled to",
  };
}

void Tap::dump(std::ostream &out) {
  out << "tap";
}

auto Tap::clone() -> Filter* {
  return new Tap(*this);
}

void Tap::reset() {
  if (m_channel) {
    m_current_account->clear(m_channel);
    delete m_channel;
    m_channel = nullptr;
  }
  if (m_session_account) {
    m_account_manager->close(m_session_account);
    m_session_account = nullptr;
  }
  m_current_account = nullptr;
  m_initialized = false;
  m_session_end = false;
}

void Tap::process(Context *ctx, Event *inp) {
  if (!m_initialized) {
    pjs::Value account, quota;
    if (!eval(*ctx, m_account, account)) return;
    if (!eval(*ctx, m_quota, quota)) return;
    if (account.is_undefined()) {
      m_session_account = m_account_manager->get();
      m_current_account = m_session_account;
    } else {
      auto *s = account.to_string();
      m_current_account = m_account_manager->get(s->str());
      s->release();
    }
    set_quota(quota);
    m_channel = new Channel(ctx, out());
    m_initialized = true;

  } else if (m_current_account && m_quota.is_function()) {
    auto now = utils::now();
    if (now - m_current_account->setup_time() >= 5000) {
      pjs::Value quota;
      if (eval(*ctx, m_quota, quota)) {
        set_quota(quota);
      }
    }
  }

  if (inp->is<SessionEnd>()) {
    if (m_channel) {
      m_current_account->clear(m_channel);
      delete m_channel;
      m_channel = nullptr;
    }
    if (m_session_account) {
      m_account_manager->close(m_session_account);
      m_session_account = nullptr;
    }
    m_current_account = nullptr;
    m_session_end = true;
    output(inp);

  } else if (m_channel) {
    m_channel->push(inp);
    m_current_account->queue(m_channel);
  }
}

void Tap::set_quota(const pjs::Value &quota) {
  if (quota.is_nullish()) {
    m_current_account->setup(-1, false);
  } else if (quota.is_number()) {
    m_current_account->setup(quota.n(), false);
  } else {
    auto *s = quota.to_string();
    m_current_account->setup(utils::get_byte_size(s->str()), true);
    s->release();
  }
}

void Tap::Account::setup(int quota, bool is_data) {
  m_initial_quota = quota;
  if (!m_is_set_up) {
    m_current_quota = quota;
    m_is_data = is_data;
    m_is_set_up = true;
  }
  m_setup_time = utils::now();
}

void Tap::Account::queue(Channel *channel) {
  m_queue.push_back(channel);
  pump();
}

void Tap::Account::clear(Channel *channel) {
  m_queue.remove(channel);
  if (m_paused_channels.erase(channel)) {
    channel->resume();
  }
}

void Tap::Account::pump() {
  while (!m_queue.empty() && (unlimited() || m_current_quota > 0)) {
    auto channel = m_queue.front();
    if (unlimited()) {
      channel->drain();
      m_queue.pop_front();
    } else if (m_is_data) {
      if (channel->deduct_by_data(m_current_quota)) {
        m_queue.pop_front();
      }
    } else {
      if (channel->deduct_by_message(m_current_quota)) {
        m_queue.pop_front();
      }
    }
  }
  if (m_queue.empty()) {
    for (auto *channel : m_paused_channels) {
      channel->resume();
    }
    m_paused_channels.clear();
  } else {
    for (auto *channel : m_queue) {
      channel->pause();
      m_paused_channels.insert(channel);
    }
  }
}

void Tap::Account::supply() {
  m_current_quota = m_initial_quota;
  pump();
}

void Tap::Channel::push(Event *inp) {
  m_buffer.push_back(inp);
}

void Tap::Channel::drain() {
  auto e = m_buffer.front().get();
  m_out(e);
  m_buffer.pop_front();
}

bool Tap::Channel::deduct_by_data(int &quota) {
  if (m_buffer.empty()) return true;
  auto e = m_buffer.front().get();
  if (auto data = e->as<Data>()) {
    if (data->size() <= quota) {
      quota -= data->size();
      m_out(e);
      m_buffer.pop_front();
      return true;
    } else {
      auto partial = Data::make();
      data->shift(quota, *partial);
      quota = 0;
      m_out(partial);
      return false;
    }
  } else {
    m_out(e);
    m_buffer.pop_front();
    return true;
  }
}

bool Tap::Channel::deduct_by_message(int &quota) {
  if (m_buffer.empty()) return true;
  auto e = m_buffer.front().get();
  if (e->is<MessageEnd>()) quota--;
  m_out(e);
  m_buffer.pop_front();
  return true;
}

void Tap::Channel::pause() {
  if (!m_paused) {
    if (auto inbound = m_ctx->inbound()) {
      inbound->pause();
    }
    m_paused = true;
  }
}

void Tap::Channel::resume() {
  if (m_paused) {
    if (auto inbound = m_ctx->inbound()) {
      inbound->resume();
    }
    m_paused = false;
  }
}

} // namespace pipy