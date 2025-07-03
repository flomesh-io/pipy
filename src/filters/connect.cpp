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

namespace pipy {

//
// Connect::Options
//

Connect::Options::Options(pjs::Object *options) {
  Value(options, "domain")
    .get(domain)
    .check_nullable();

  if (domain > 0) {
    Value(options, "type")
      .get_enum(type)
      .check_nullable();
    Value(options, "protocol")
      .get(protocol)
      .check_nullable();
    Value(options, "bind")
      .get(bind_d)
      .get(bind_f)
      .check_nullable();
  } else {
    Value(options, "protocol")
      .get_enum(protocol_inet)
      .check_nullable();
    Value(options, "bind")
      .get(bind)
      .get(bind_f)
      .check_nullable();
  }

  Value(options, "netlinkFamily")
    .get(netlink_family)
    .check_nullable();

  Value(options, "onState")
    .get(on_state_f)
    .check_nullable();
  Value(options, "congestionLimit")
    .get_binary_size(congestion_limit)
    .check_nullable();
  Value(options, "bufferLimit")
    .get_binary_size(buffer_limit)
    .check_nullable();
  Value(options, "retryCount")
    .get(retry_count)
    .check_nullable();
  Value(options, "retryDelay")
    .get_seconds(retry_delay)
    .check_nullable();
  Value(options, "connectTimeout")
    .get_seconds(connect_timeout)
    .check_nullable();
  Value(options, "readTimeout")
    .get_seconds(read_timeout)
    .check_nullable();
  Value(options, "writeTimeout")
    .get_seconds(write_timeout)
    .check_nullable();
  Value(options, "idleTimeout")
    .get_seconds(idle_timeout)
    .check_nullable();
  Value(options, "keepAlive")
    .get(keep_alive)
    .check_nullable();
  Value(options, "noDelay")
    .get(no_delay)
    .check_nullable();
}

//
// Connect
//

Connect::Connect(const pjs::Value &target, const Options &options)
  : m_target(target)
  , m_options(options)
{
}

Connect::Connect(const pjs::Value &target, pjs::Function *options)
  : m_target(target)
  , m_options_f(options)
{
}

Connect::Connect(const Connect &r)
  : Filter(r)
  , m_target(r.m_target)
  , m_options_f(r.m_options_f)
  , m_options(r.m_options)
{
}

Connect::~Connect() {
}

void Connect::dump(Dump &d) {
  Filter::dump(d);
  d.name = "connect";
}

auto Connect::clone() -> Filter* {
  return new Connect(*this);
}

void Connect::reset() {
  Filter::reset();
  if (m_outbound) {
    m_outbound->close();
    m_outbound = nullptr;
  }
  m_has_error = false;
  m_end_input = false;
}

void Connect::process(Event *evt) {
  if (m_has_error || m_end_input) return;

  if (!m_outbound) {
    if (evt->is<StreamEnd>()) {
      Filter::output(evt);
      return end();
    }

    Options eval_options;
    if (auto f = m_options_f.get()) {
      pjs::Value ret;
      if (!Filter::eval(f, ret)) return error();
      if (!ret.is_object()) {
        Filter::error("invalid options");
        return error();
      }
      try {
        eval_options = Options(ret.o());
      } catch (std::runtime_error &err) {
        Filter::error(err.what());
        return error();
      }
    }

    auto &options = m_options_f ? eval_options : m_options;

    if (options.on_state_f) {
      pjs::Ref<pjs::Function> f = options.on_state_f;
      options.on_state_changed = [=](Outbound *ob) {
        pjs::Value arg(ob), ret;
        Filter::callback(f, 1, &arg, ret);
      };
    }

    if (options.domain > 0) {
      switch (options.type) {
      case Outbound::Type::STREAM:
        m_outbound = OutboundStream::make(Filter::output(), options);
        break;
      case Outbound::Type::DATAGRAM:
        m_outbound = OutboundDatagram::make(Filter::output(), options);
        break;
      case Outbound::Type::RAW:
        m_outbound = OutboundRaw::make(Filter::output(), options);
        break;
      }
      if (m_outbound) m_outbound->open();

    } else {
      switch (options.protocol_inet) {
      case Outbound::Protocol::TCP:
        m_outbound = OutboundTCP::make(Filter::output(), options);
        break;
      case Outbound::Protocol::UDP:
        m_outbound = OutboundUDP::make(Filter::output(), options);
        break;
      }
    }

    pjs::Ref<pjs::Str> bind(options.bind);
    pjs::Ref<Data> bind_data(options.bind_d);

    if (options.bind_f) {
      pjs::Value ret;
      if (!Filter::eval(options.bind_f, ret)) return error();
      if (!ret.is_undefined()) {
        if (ret.is_string()) {
          bind = ret.s();
        } else if (ret.is<Data>()) {
          bind_data = ret.as<Data>();
        } else {
          Filter::error("invalid bind address");
          return error();
        }
      }
    }

    try {
      if (bind) {
        m_outbound->bind(bind->str());
      } else if (bind_data) {
        pjs::vl_array<uint8_t, 1000> addr_buf(bind_data->size());
        bind_data->to_bytes(addr_buf.data());
        m_outbound->bind(addr_buf, bind_data->size());
      }
    } catch (std::runtime_error &e) {
      Filter::error("%s", e.what());
      return error();
    }

    IPEndpoint *ep = nullptr;
    Data *addr_data = nullptr;

    pjs::Value target;
    if (!eval(m_target, target)) {
      return error();
    }

    if (target.is<IPEndpoint>()) {
      ep = target.as<IPEndpoint>();
      if (!ep->ip) {
        Filter::error("invalid IP address");
        return error();
      }
    } else if (target.is<Data>()) {
      addr_data = target.as<Data>();
    } else if (!target.is_string()) {
      Filter::error("invalid target");
      return error();
    }

    try {
      if (ep) {
        m_outbound->connect(ep->ip, ep->port);
      } else if (addr_data) {
        pjs::vl_array<uint8_t, 1000> addr_buf(addr_data->size());
        addr_data->to_bytes(addr_buf.data());
        m_outbound->connect(addr_buf, addr_data->size());
      } else {
        m_outbound->connect(target.s()->str());
      }

    } catch (std::runtime_error &e) {
      Filter::error("%s", e.what());
      return error();
    }
  }

  if (m_outbound) {
    m_outbound->send(evt);
    if (evt->is<StreamEnd>()) {
      m_end_input = true;
    }
  }
}

} // namespace pipy
