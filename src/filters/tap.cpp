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
  : m_accounts(std::make_shared<Accounts>())
  , m_quota(quota)
  , m_account(account)
{
}

Tap::Tap(const Tap &r)
  : m_accounts(r.m_accounts)
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
  m_queue = nullptr;
  m_initialized = false;
  m_session_end = false;
}

void Tap::process(Context *ctx, Event *inp) {
  if (!m_initialized) {
    pjs::Value account_name, quota;
    if (!eval(*ctx, m_account, account_name)) return;
    if (!eval(*ctx, m_quota, quota)) return;
    auto *s = account_name.to_string();
    m_queue = m_accounts->get(s->str());
    s->release();
    if (quota.is_number()) {
      m_queue->setup(quota.n(), false);
    } else {
      auto *s = quota.to_string();
      m_queue->setup(utils::get_byte_size(s->str()), true);
      s->release();
    }
    m_initialized = true;
  }

  if (inp->is<SessionEnd>()) {
    m_queue = nullptr;
    m_session_end = true;
    output(inp);

  } else if (m_queue) {
    m_queue->push(ctx, inp, [=](const pjs::Ref<Event> &inp) { output(inp); });
  }
}

void Tap::Queue::setup(int quota, bool is_data) {
  m_initial_quota = quota;
  m_is_data = is_data;
  if (!m_has_set) {
    m_current_quota = quota;
    m_has_set = true;
  }
}

void Tap::Queue::push(Context *ctx, Event *e, Event::Receiver out) {
  m_queue.emplace_back();
  auto &i = m_queue.back();
  i.event = e;
  i.out = out;
  pump();
  if (!m_queue.empty()) {
    if (auto inbound = ctx->inbound()) {
      m_paused_contexts.insert(ctx);
      inbound->pause();
    }
  }
}

void Tap::Queue::pump() {
  while (!m_queue.empty() && (m_initial_quota < 0 || m_current_quota > 0)) {
    auto &head = m_queue.front();
    auto e = head.event;
    auto f = head.out;
    if (m_is_data) {
      auto partial = deduct_data(e);
      if (partial) {
        f(partial);
      } else {
        m_queue.pop_front();
        f(e);
      }
    } else {
      deduct_message(e);
      m_queue.pop_front();
      f(e);
    }
  }
  if (m_queue.empty()) {
    for (const auto &ctx : m_paused_contexts) {
      if (auto inbound = ctx->inbound()) {
        inbound->resume();
      }
    }
    m_paused_contexts.clear();
  }
  supply();
}

void Tap::Queue::supply() {
  if (m_supplying) return;
  m_timer.schedule(1.0, [=]() {
    m_current_quota = m_initial_quota;
    m_supplying = false;
    pump();
  });
  m_supplying = true;
}

auto Tap::Queue::deduct_data(Event *e) -> Data* {
  if (m_initial_quota < 0) return nullptr;
  if (e->is<Data>()) {
    auto data = e->as<Data>();
    if (data->size() <= m_current_quota) {
      m_current_quota -= data->size();
      return nullptr;
    } else {
      auto partial = Data::make();
      data->shift(m_current_quota, *partial);
      m_current_quota = 0;
      return partial;
    }
  }
  return nullptr;
}

void Tap::Queue::deduct_message(Event *e) {
  if (m_initial_quota < 0) return;
  if (e->is<MessageEnd>()) {
    m_current_quota--;
  }
}

} // namespace pipy