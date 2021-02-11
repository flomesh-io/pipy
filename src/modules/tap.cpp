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
#include "logging.hpp"
#include "utils.hpp"

NS_BEGIN

//
// Tap
//

Tap::Tap()
  : m_shared_control(new SharedControl())
{
}

Tap::Tap(const Tap &other)
  : m_shared_control(other.m_shared_control)
{
}

Tap::~Tap() {
}

auto Tap::help() -> std::list<std::string> {
  return {
    "Limits the message rate of the stream",
    "limit = Maximum number of messages allowed per second",
  };
}

void Tap::config(const std::map<std::string, std::string> &params) {
  auto limit = std::atoi(utils::get_param(params, "limit").c_str());
  m_shared_control->config(limit);
}

auto Tap::clone() -> Module* {
  return new Tap(*this);
}

void Tap::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>()) {
    m_buffer.clear();
    m_context_id = ctx->id;
    m_is_blocking = false;
    out(std::move(obj));

  } else if (obj->is<SessionEnd>()) {
    m_context_id = 0;
    out(std::move(obj));

  } else if (obj->is<MessageStart>()) {
    auto context_id = ctx->id;
    auto ret = m_shared_control->request_quota([=]() {
      if (context_id == m_context_id) {
        drain(out);
      }
    });
    if (!ret) {
      m_is_blocking = true;
      m_buffer.push_back(std::move(obj));
    } else {
      out(std::move(obj));
    }

  } else if (m_is_blocking) {
    m_buffer.push_back(std::move(obj));

  } else {
    out(std::move(obj));
  }
}

void Tap::drain(Object::Receiver out) {
  for (auto &p : m_buffer) {
    out(std::move(p));
  }
  m_buffer.clear();
  m_is_blocking = false;
}

//
// Tap::SharedControl
//

void Tap::SharedControl::config(int limit) {
  m_limit = limit;
  m_quota = limit;
}

bool Tap::SharedControl::request_quota(std::function<void()> drainer) {
  auto now = std::chrono::steady_clock::now();
  auto duration = now - m_window_start;
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

  if (duration_ms >= 1000) {
    m_window_start = now;
    m_quota = m_limit;
  }

  if (m_is_draining) {
    m_drainers.push_back(drainer);
    return false;
  }

  if (--m_quota < 0) {
    m_drainers.push_back(drainer);
    m_is_draining = true;
    Listener::set_timeout(1.0 - (duration_ms / 1000.0), [=]() {
      m_window_start = std::chrono::steady_clock::now();
      m_quota = m_limit;
      drain();
    });
    return false;
  }

  return true;
}

void Tap::SharedControl::drain() {
  int count = 0;
  while (m_quota > 0 && !m_drainers.empty()) {
    m_drainers.front()();
    m_drainers.pop_front();
    m_quota--;
    count++;
  }

  if (count > 0) {
    Log::info("[tap] %d request(s) got delayed", count);
  }

  if (m_drainers.empty()) {
    m_is_draining = false;

  } else {
    m_is_draining = true;
    Listener::set_timeout(1.0, [=]() {
      m_window_start = std::chrono::steady_clock::now();
      m_quota = m_limit;
      drain();
    });
  }
}

NS_END

