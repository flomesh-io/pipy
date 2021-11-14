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
#include "outbound.hpp"
#include "utils.hpp"
#include "logging.hpp"

namespace pipy {

Connect::Connect(const pjs::Value &target, pjs::Object *options)
  : m_target(target)
{
  if (options) {
    pjs::Value buffer_limit, retry_count, retry_delay;
    pjs::Value connect_timeout, read_timeout, write_timeout;
    options->get("bufferLimit", buffer_limit);
    options->get("retryCount", retry_count);
    options->get("retryDelay", retry_delay);
    options->get("connectTimeout", connect_timeout);
    options->get("readTimeout", read_timeout);
    options->get("writeTimeout", write_timeout);

    if (!buffer_limit.is_undefined()) {
      if (buffer_limit.is_string()) {
        m_options.buffer_limit = utils::get_byte_size(buffer_limit.s()->str());
      } else {
        m_options.buffer_limit = buffer_limit.to_number();
      }
    }

    if (!retry_count.is_undefined()) m_options.retry_count = retry_count.to_number();

    if (!retry_delay.is_undefined()) {
      if (retry_delay.is_string()) {
        m_options.retry_delay = utils::get_seconds(retry_delay.s()->str());
      } else {
        m_options.retry_delay = retry_delay.to_number();
      }
    }

    if (!connect_timeout.is_undefined()) {
      if (connect_timeout.is_string()) {
        m_options.connect_timeout = utils::get_seconds(connect_timeout.s()->str());
      } else {
        m_options.connect_timeout = connect_timeout.to_number();
      }
    }

    if (!read_timeout.is_undefined()) {
      if (read_timeout.is_string()) {
        m_options.read_timeout = utils::get_seconds(read_timeout.s()->str());
      } else {
        m_options.read_timeout = read_timeout.to_number();
      }
    }

    if (!write_timeout.is_undefined()) {
      if (write_timeout.is_string()) {
        m_options.write_timeout = utils::get_seconds(write_timeout.s()->str());
      } else {
        m_options.write_timeout = write_timeout.to_number();
      }
    }
  }
}

Connect::Connect(const pjs::Value &target, const Outbound::Options &options)
  : m_target(target)
  , m_options(options)
{
}

Connect::Connect(const Connect &r)
  : Filter(r)
  , m_target(r.m_target)
  , m_options(r.m_options)
{
}

Connect::~Connect() {
}

void Connect::dump(std::ostream &out) {
  out << "connect";
}

auto Connect::clone() -> Filter* {
  return new Connect(*this);
}

void Connect::reset() {
  Filter::reset();
  if (m_output) {
    m_output->close();
    m_output = nullptr;
  }
  if (m_outbound) {
    m_outbound->end();
    m_outbound = nullptr;
  }
}

void Connect::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    if (m_outbound) {
      m_outbound->end();
      m_outbound = nullptr;
    }
    return;
  }

  if (!m_outbound) {
    pjs::Value target;
    if (!eval(m_target, target)) return;
    auto s = target.to_string();
    std::string host; int port;
    if (utils::get_host_port(s->str(), host, port)) {
      m_output = Input::make(output());
      auto outbound = new Outbound(m_output, m_options);
      outbound->connect(host, port);
      m_outbound = outbound;
    } else {
      Log::error("[connect] invalid target: %s", s->c_str());
    }
    s->release();
  }

  if (m_outbound) {
    if (auto *data = evt->as<Data>()) {
      m_outbound->send(data);
    } else if (evt->is<MessageEnd>()) {
      m_outbound->flush();
    }
  }
}

} // namespace pipy
