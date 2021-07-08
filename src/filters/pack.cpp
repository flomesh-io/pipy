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

#include "pack.hpp"

namespace pipy {

//
// Pack
//

Pack::Pack()
{
}

Pack::Pack(int batch_size, pjs::Object *options)
  : m_batch_size(batch_size)
{
  if (options) {
    pjs::Value val;

    options->get("timeout", val);
    if (val.is_number()) {
      m_timeout = val.n();
    } else if (val.is_string()) {
      m_timeout = utils::get_seconds(val.s()->str());
    } else if (!val.is_undefined()) {
      throw std::runtime_error("options.timeout expects a number or a string");
    }

    options->get("vacancy", val);
    if (val.is_number()) {
      m_vacancy = val.n();
      if (m_vacancy < 0 || m_vacancy > 1) {
        throw std::runtime_error("options.m_vacancy expects a number between 0 and 1");
      }
    } else if (!val.is_undefined()) {
      throw std::runtime_error("options.m_vacancy expects a number");
    }
  }
}

Pack::Pack(const Pack &r)
  : m_batch_size(r.m_batch_size)
  , m_timeout(r.m_timeout)
{
  if (m_timeout > 0) {
    m_timer = std::unique_ptr<Timer>(new Timer);
    schedule_timeout();
  }
}

Pack::~Pack()
{
}

auto Pack::help() -> std::list<std::string> {
  return {
    "pack([batchSize[, options]])",
    "Packs data of one or more messages into one message and squeezes out spare room in the data chunks",
    "batchSize = <int> Number of messages to pack in one. Defaults to 1",
    "options = <object> Options including timeout, vacancy",
  };
}

void Pack::dump(std::ostream &out) {
  out << "pack";
}

auto Pack::clone() -> Filter* {
  return new Pack(*this);
}

void Pack::reset() {
  m_timer = nullptr;
  m_message_starts = 0;
  m_message_ends = 0;
  m_session_end = false;
}

void Pack::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (m_timeout > 0) {
    if (!m_timer) {
      m_timer = std::unique_ptr<Timer>(new Timer);
      schedule_timeout();
    }
  }

  if (auto e = inp->as<MessageStart>()) {
    if (m_message_starts == 0) {
      m_mctx = e->context();
      m_head = e->head();
      m_buffer = Data::make();
    }
    m_message_starts++;
    return;

  } else if (auto data = inp->as<Data>()) {
    if (m_buffer) {
      m_buffer->pack(*data, m_vacancy);
      return;
    }

  } else if (auto *end = inp->as<MessageEnd>()) {
    m_message_ends++;
    if (m_message_starts == m_message_ends && m_message_ends >= m_batch_size) {
      flush(end);
    } else if (m_timeout > 0) {
      m_last_input_time = std::chrono::steady_clock::now();
    }

  } else if (inp->is<SessionEnd>()) {
    output(inp);
    m_session_end = true;
  }
}

void Pack::flush(MessageEnd *end) {
  output(MessageStart::make(m_mctx, m_head));
  output(m_buffer);
  output(end);
  m_mctx = nullptr;
  m_head = nullptr;
  m_buffer = nullptr;
  m_message_starts = 0;
  m_message_ends = 0;
}

void Pack::schedule_timeout() {
  auto precision = std::min(m_timeout, 1.0);
  m_timer->schedule(
    precision,
    [this]() { check_timeout(); }
  );
}

void Pack::check_timeout() {
  if (m_message_ends > 0 && m_message_ends == m_message_starts) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_input_time).count() >= m_timeout * 1000) {
      flush(MessageEnd::make());
    }
  }

  // m_timer could've been gone after a flush
  if (m_timer) {
    schedule_timeout();
  }
}

} // namespace pipy