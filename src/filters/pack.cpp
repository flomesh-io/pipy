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
#include "input.hpp"

namespace pipy {

//
// Pack::Options
//

Pack::Options::Options(pjs::Object *options) {
  Value(options, "vacancy")
    .get(vacancy)
    .check_nullable();
  Value(options, "timeout")
    .get_seconds(timeout)
    .check_nullable();
  Value(options, "interval")
    .get_seconds(interval)
    .check_nullable();
}

//
// Pack
//

Pack::Pack(int batch_size, const Options &options)
  : m_batch_size(batch_size)
  , m_options(options)
{
}

Pack::Pack(const Pack &r)
  : Filter(r)
  , m_batch_size(r.m_batch_size)
  , m_options(r.m_options)
{
}

Pack::~Pack()
{
}

void Pack::dump(std::ostream &out) {
  out << "pack";
}

auto Pack::clone() -> Filter* {
  return new Pack(*this);
}

void Pack::reset() {
  Filter::reset();
  m_timer.cancel();
  m_timer_scheduled = false;
  m_message_starts = 0;
  m_message_ends = 0;
}

void Pack::process(Event *evt) {
  static Data::Producer s_dp("pack");

  schedule_timeout();

  if (auto start = evt->as<MessageStart>()) {
    if (m_message_starts == 0) {
      m_head = start->head();
      m_buffer = Data::make();
    }
    m_message_starts++;

  } else if (auto data = evt->as<Data>()) {
    if (m_buffer) {
      s_dp.pack(m_buffer, data, m_options.vacancy);
    }

  } else if (auto *end = evt->as<MessageEnd>()) {
    m_message_ends++;
    if (m_options.timeout > 0 || m_options.interval > 0) {
      m_last_input_time = utils::now() / 1000;
    }
    if (
      (m_message_starts == m_message_ends && m_message_ends >= m_batch_size) ||
      (m_options.interval > 0 && m_last_input_time - m_last_flush_time >= m_options.interval)
    ) {
      flush(end);
    }

  } else if (evt->is<StreamEnd>()) {
    if (m_message_starts == m_message_ends && m_message_ends > 0) {
      flush(MessageEnd::make());
    }
    output(evt);
  }
}

void Pack::flush(MessageEnd *end) {
  output(MessageStart::make(m_head));
  output(m_buffer);
  output(end);
  m_head = nullptr;
  m_buffer = nullptr;
  m_message_starts = 0;
  m_message_ends = 0;
  if (m_options.interval > 0) {
    m_last_flush_time = utils::now() / 1000;
  }
}

void Pack::schedule_timeout() {
  if (!m_timer_scheduled && m_options.timeout > 0) {
    auto precision = std::min(m_options.timeout, 1.0);
    m_timer.schedule(
      precision,
      [this]() {
        check_timeout();
      }
    );
    m_timer_scheduled = true;
  }
}

void Pack::check_timeout() {
  if (m_message_ends > 0 && m_message_ends == m_message_starts) {
    auto now = utils::now() / 1000;
    if (now - m_last_input_time >= m_options.timeout) {
      InputContext ic;
      flush(MessageEnd::make());
    }
  }

  schedule_timeout();
}

} // namespace pipy
