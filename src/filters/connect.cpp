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

#include "connect.hpp"
#include "worker.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "outbound.hpp"
#include "context.hpp"
#include "utils.hpp"
#include "logging.hpp"

namespace pipy {

Connect::Connect()
{
}

Connect::Connect(const pjs::Value &target, pjs::Object *options)
  : m_target(target)
{
  if (options) {
    pjs::Value buffer_limit, retry_count, retry_delay;
    options->get("bufferLimit", buffer_limit);
    options->get("retryCount", retry_count);
    options->get("retryDelay", retry_delay);

    if (!buffer_limit.is_undefined()) {
      if (buffer_limit.is_string()) {
        m_buffer_limit = utils::get_byte_size(buffer_limit.s()->str());
      } else {
        m_buffer_limit = buffer_limit.to_number();
      }
    }

    if (!retry_count.is_undefined()) m_retry_count = retry_count.to_number();

    if (!retry_delay.is_undefined()) {
      if (retry_delay.is_string()) {
        m_retry_delay = utils::get_seconds(retry_delay.s()->str());
      } else {
        m_retry_delay = retry_delay.to_number();
      }
    }
  }
}

Connect::Connect(const Connect &r)
  : m_target(r.m_target)
  , m_buffer_limit(r.m_buffer_limit)
  , m_retry_count(r.m_retry_count)
  , m_retry_delay(r.m_retry_delay)
{
}

Connect::~Connect() {
}

auto Connect::help() -> std::list<std::string> {
  return {
    "connect(target[, options])",
    "Sends data to a remote endpoint and receives data from it",
    "target = <string|function> Remote endpoint in the form of `<ip>:<port>`",
    "options = <object> Includes bufferLimit, retryCount, retryDelay",
  };
}

void Connect::dump(std::ostream &out) {
  out << "connect";
}

auto Connect::clone() -> Filter* {
  return new Connect(*this);
}

void Connect::reset() {
  if (m_outbound) {
    m_outbound->on_receive(nullptr);
    m_outbound->on_delete(nullptr);
    m_outbound->end();
    m_outbound = nullptr;
  }
  m_session_end = false;
}

void Connect::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (inp->is<SessionEnd>()) {
    if (m_outbound) {
      m_outbound->end();
    }
    m_session_end = true;
    return;
  }

  if (!m_outbound) {
    pjs::Value target;
    if (eval(*ctx, m_target, target)) {
      auto s = target.to_string();
      std::string host; int port;
      if (utils::get_host_port(s->str(), host, port)) {
        auto outbound = new Outbound;
        outbound->set_buffer_limit(m_buffer_limit);
        outbound->set_retry_count(m_retry_count);
        outbound->set_retry_delay(m_retry_delay);
        outbound->on_delete([this]() { m_outbound = nullptr; });
        outbound->on_receive([=](Event *inp) {
          auto safe_ctx = ctx;
          safe_ctx->retain();
          output(inp);
          if (safe_ctx->ref_count() > 1) {
            safe_ctx->group()->notify(safe_ctx);
          }
          safe_ctx->release();
        });
        outbound->connect(host, port);
        m_outbound = outbound;
      } else {
        m_session_end = true;
        Log::warn("[connect] invalid target: %s", s->c_str());
      }
      s->release();
    }
  }

  if (m_outbound) {
    if (auto *data = inp->as<Data>()) {
      m_outbound->send(data);
    } else if (inp->is<MessageEnd>()) {
      m_outbound->flush();
    }
  }
}

} // namespace pipy