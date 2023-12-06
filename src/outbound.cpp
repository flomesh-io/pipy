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

#include "outbound.hpp"
#include "constants.hpp"
#include "pipeline.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <iostream>

#ifdef __linux__
#include <linux/netlink.h>
#endif

namespace pipy {

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;

//
// Outbound
//

thread_local List<Outbound> Outbound::s_all_outbounds;
thread_local pjs::Ref<stats::Gauge> Outbound::s_metric_concurrency;
thread_local pjs::Ref<stats::Counter> Outbound::s_metric_traffic_in;
thread_local pjs::Ref<stats::Counter> Outbound::s_metric_traffic_out;
thread_local pjs::Ref<stats::Histogram> Outbound::s_metric_conn_time;

Outbound::Outbound(EventTarget::Input *input, const Options &options)
  : m_options(options)
  , m_input(input)
{
  init_metrics();
  Log::debug(Log::ALLOC, "[outbound %p] ++", this);
  s_all_outbounds.push(this);
}

Outbound::~Outbound() {
  Log::debug(Log::ALLOC, "[outbound %p] --", this);
  s_all_outbounds.remove(this);
}

auto Outbound::protocol_name() const -> pjs::Str* {
  thread_local static pjs::ConstStr s_TCP("TCP");
  thread_local static pjs::ConstStr s_UDP("UDP");
  thread_local static pjs::ConstStr s_Netlink("Netlink");
  switch (m_options.protocol) {
    case Protocol::TCP: return s_TCP;
    case Protocol::UDP: return s_UDP;
    case Protocol::NETLINK: return s_Netlink;
  }
  return nullptr;
}

auto Outbound::local_address() -> pjs::Str* {
  if (!m_local_addr_str) {
    m_local_addr_str = pjs::Str::make(m_local_addr);
  }
  return m_local_addr_str;
}

auto Outbound::remote_address() -> pjs::Str* {
  if (!m_remote_addr_str) {
    m_remote_addr_str = pjs::Str::make(m_remote_addr);
  }
  return m_remote_addr_str;
}

auto Outbound::address() -> pjs::Str* {
  if (!m_address) {
    std::string s("[");
    s += m_host;
    s += "]:";
    s += std::to_string(m_port);
    m_address = pjs::Str::make(std::move(s));
  }
  return m_address;
}

void Outbound::close(StreamEnd *eos) {
  InputContext ic;
  retain();
  input(eos);
  close();
  release();
}

void Outbound::state(State state) {
  if (m_state != state) {
    m_state = state;
    if (const auto &f = m_options.on_state_changed) {
      f(this);
    }
  }
}

void Outbound::input(Event *evt) {
  if (m_state != State::closed) {
    m_input->input(evt);
  }
}

void Outbound::error(StreamEnd::Error err) {
  m_error = err;
  input(StreamEnd::make(err));
  state(State::closed);
}

void Outbound::describe(char *buf, size_t len) {
  std::snprintf(
    buf, len,
    "[outbound %p] [%s]:%d -> [%s]:%d (%s)",
    this,
    m_local_addr.empty() ? "0.0.0.0" : m_local_addr.c_str(),
    m_local_port,
    m_remote_addr.c_str(),
    m_port,
    m_host.c_str()
  );
}

void Outbound::collect() {
  auto in = get_traffic_in();
  auto out = get_traffic_out();
  s_metric_traffic_in->increase(in);
  s_metric_traffic_out->increase(out);
  if (m_metric_traffic_in) m_metric_traffic_in->increase(in);
  if (m_metric_traffic_out) m_metric_traffic_out->increase(out);
}

void Outbound::to_ip_addr(const std::string &address, std::string &host, int &port, int default_port) {
  if (!utils::get_host_port(address, host, port)) {
    if (default_port >= 0) {
      host = address;
      port = default_port;
    } else {
      std::string msg("invalid address format: ");
      throw std::runtime_error(msg + address);
    }
  }
}

void Outbound::init_metrics() {
  if (!s_metric_concurrency) {
    pjs::Ref<pjs::Array> label_names = pjs::Array::make();
    label_names->length(2);
    label_names->set(0, "protocol");
    label_names->set(1, "peer");

    s_metric_concurrency = stats::Gauge::make(
      pjs::Str::make("pipy_outbound_count"),
      label_names,
      [=](stats::Gauge *gauge) {
        int total = 0;
        gauge->zero_all();
        Outbound::for_each([&](Outbound *outbound) {
          pjs::Str *k[2];
          k[0] = outbound->protocol_name();
          k[1] = outbound->address();
          auto cnt = gauge->with_labels(k, 2);
          cnt->increase();
          total++;
          return true;
        });
        gauge->set(total);
      }
    );

    s_metric_traffic_in = stats::Counter::make(
      pjs::Str::make("pipy_outbound_in"),
      label_names,
      [=](stats::Counter *counter) {
        Outbound::for_each([&](Outbound *outbound) {
          auto n = outbound->get_traffic_in();
          outbound->m_metric_traffic_in->increase(n);
          s_metric_traffic_in->increase(n);
          return true;
        });
      }
    );

    s_metric_traffic_out = stats::Counter::make(
      pjs::Str::make("pipy_outbound_out"),
      label_names,
      [=](stats::Counter *counter) {
        Outbound::for_each([&](Outbound *outbound) {
          auto n = outbound->get_traffic_out();
          outbound->m_metric_traffic_out->increase(n);
          s_metric_traffic_out->increase(n);
          return true;
        });
      }
    );

    pjs::Ref<pjs::Array> buckets = pjs::Array::make(21);
    double limit = 1.5;
    for (int i = 0; i < 20; i++) {
      buckets->set(i, std::floor(limit));
      limit *= 1.5;
    }
    buckets->set(20, std::numeric_limits<double>::infinity());

    s_metric_conn_time = stats::Histogram::make(
      pjs::Str::make("pipy_outbound_conn_time"),
      buckets, label_names
    );
  }
}

//
// OutboundTCP
//

OutboundTCP::OutboundTCP(EventTarget::Input *output, const Outbound::Options &options)
  : pjs::ObjectTemplate<OutboundTCP, Outbound>(output, options)
  , SocketTCP(false, Outbound::m_options)
  , m_resolver(Net::context())
{
}

OutboundTCP::~OutboundTCP() {
  Outbound::collect();
}

void OutboundTCP::bind(const std::string &address) {
  std::string ip;
  int port;
  to_ip_addr(address, ip, port, 0);
  auto &s = SocketTCP::socket();
  tcp::endpoint ep(asio::ip::make_address(ip), port);
  s.open(ep.protocol());
  s.bind(ep);
  const auto &local = s.local_endpoint();
  m_local_addr = local.address().to_string();
  m_local_port = local.port();
  m_local_addr_str = nullptr;
}

void OutboundTCP::connect(const std::string &address) {
  to_ip_addr(address, m_host, m_port);
  pjs::Str *keys[2];
  keys[0] = protocol_name();
  keys[1] = Outbound::address();
  m_metric_traffic_out = Outbound::s_metric_traffic_out->with_labels(keys, 2);
  m_metric_traffic_in = Outbound::s_metric_traffic_in->with_labels(keys, 2);
  m_metric_conn_time = Outbound::s_metric_conn_time->with_labels(keys, 2);

  start(0);
}

void OutboundTCP::send(Event *evt) {
  SocketTCP::output(evt);
}

void OutboundTCP::close() {
  asio::error_code ec;
  switch (state()) {
    case Outbound::State::resolving:
    case Outbound::State::connecting:
      m_resolver.cancel();
      m_connect_timer.cancel();
      SocketTCP::socket().cancel(ec);
      break;
    case Outbound::State::connected:
      SocketTCP::close();
      break;
    default: break;
  }
  state(Outbound::State::closed);
}

void OutboundTCP::start(double delay) {
  if (delay > 0) {
    m_retry_timer.schedule(
      delay,
      [this]() {
        resolve();
      }
    );
    state(Outbound::State::idle);
  } else {
    resolve();
  }
}

void OutboundTCP::resolve() {
  static const std::string s_localhost("localhost");
  static const std::string s_localhost_ip("127.0.0.1");
  const auto &host = (m_host == s_localhost ? s_localhost_ip : m_host);

  m_resolver.async_resolve(
    tcp::resolver::query(host, std::to_string(m_port)),
    [this](
      const std::error_code &ec,
      tcp::resolver::results_type results
    ) {
      InputContext ic;

      if (ec && options().connect_timeout > 0) {
        m_connect_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[1000];
            describe(desc, sizeof(desc));
            Log::error("%s cannot resolve hostname: %s", desc, ec.message().c_str());
          }
          connect_error(StreamEnd::CANNOT_RESOLVE);

        } else if (state() == Outbound::State::resolving) {
          auto &result = *results;
          const auto &target = result.endpoint();
          m_remote_addr = target.address().to_string();
          m_remote_addr_str = nullptr;
          connect(target);
        }
      }

      release();
    }
  );

  log_debug("resolving hostname...");

  if (options().connect_timeout > 0) {
    m_connect_timer.schedule(
      options().connect_timeout,
      [this]() {
        connect_error(StreamEnd::CONNECTION_TIMEOUT);
      }
    );
  }

  m_start_time = utils::now();

  if (m_retries > 0) {
    if (Log::is_enabled(Log::WARN)) {
      char desc[200];
      describe(desc, sizeof(desc));
      Log::warn("%s retry connecting... (retries = %d)", desc, m_retries);
    }
  }

  retain();
  state(Outbound::State::resolving);
}

void OutboundTCP::connect(const asio::ip::tcp::endpoint &target) {
  socket().async_connect(
    target,
    [=](const std::error_code &ec) {
      InputContext ic;

      if (options().connect_timeout > 0) {
        m_connect_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc, sizeof(desc));
            Log::error("%s cannot connect: %s", desc, ec.message().c_str());
          }
          connect_error(StreamEnd::CONNECTION_REFUSED);

        } else if (state() == Outbound::State::connecting) {
          const auto &ep = socket().local_endpoint();
          m_local_addr = ep.address().to_string();
          m_local_port = ep.port();
          m_local_addr_str = nullptr;

          auto conn_time = utils::now() - m_start_time;
          m_connection_time += conn_time;
          m_metric_conn_time->observe(conn_time);
          s_metric_conn_time->observe(conn_time);

          if (Log::is_enabled(Log::OUTBOUND)) {
            char desc[200];
            describe(desc, sizeof(desc));
            Log::debug(Log::OUTBOUND, "%s connected in %g ms", desc, conn_time);
          }

          state(Outbound::State::connected);
          retain();
          SocketTCP::open();
        }
      }

      release();
    }
  );

  if (Log::is_enabled(Log::OUTBOUND)) {
    char desc[200];
    describe(desc, sizeof(desc));
    Log::debug(Log::OUTBOUND, "%s connecting...", desc);
  }

  retain();
  state(Outbound::State::connecting);
}

void OutboundTCP::connect_error(StreamEnd::Error err) {
  if (options().retry_count >= 0 && m_retries >= options().retry_count) {
    error(err);
  } else {
    m_retries++;
    std::error_code ec;
    socket().close(ec);
    m_resolver.cancel();
    state(Outbound::State::idle);
    start(options().retry_delay);
  }
}

auto OutboundTCP::get_traffic_in() -> size_t {
  auto n = SocketTCP::m_traffic_read;
  SocketTCP::m_traffic_read = 0;
  return n;
}

auto OutboundTCP::get_traffic_out() -> size_t {
  auto n = SocketTCP::m_traffic_write;
  SocketTCP::m_traffic_write = 0;
  return n;
}

//
// OutboundUDP
//

OutboundUDP::OutboundUDP(EventTarget::Input *output, const Outbound::Options &options)
  : pjs::ObjectTemplate<OutboundUDP, Outbound>(output, options)
  , SocketUDP(false, Outbound::m_options)
  , m_resolver(Net::context())
{
}

OutboundUDP::~OutboundUDP() {
  Outbound::collect();
}

void OutboundUDP::bind(const std::string &address) {
  std::string ip;
  int port;
  to_ip_addr(address, ip, port, 0);
  auto &s = SocketUDP::socket();
  udp::endpoint ep(asio::ip::make_address(ip), port);
  s.open(ep.protocol());
  s.bind(ep);
  const auto &local = s.local_endpoint();
  m_local_addr = local.address().to_string();
  m_local_port = local.port();
  m_local_addr_str = nullptr;
}

void OutboundUDP::connect(const std::string &address) {
  to_ip_addr(address, m_host, m_port);
  pjs::Str *keys[2];
  keys[0] = protocol_name();
  keys[1] = Outbound::address();
  m_metric_traffic_out = Outbound::s_metric_traffic_out->with_labels(keys, 2);
  m_metric_traffic_in = Outbound::s_metric_traffic_in->with_labels(keys, 2);
  m_metric_conn_time = Outbound::s_metric_conn_time->with_labels(keys, 2);

  start(0);
}

void OutboundUDP::send(Event *evt) {
  SocketUDP::output(evt);
}

void OutboundUDP::close() {
  asio::error_code ec;
  switch (state()) {
    case State::resolving:
    case State::connecting:
      m_resolver.cancel();
      m_connect_timer.cancel();
      SocketUDP::socket().cancel(ec);
      break;
    case State::connected:
      SocketUDP::close();
      break;
    default: break;
  }
  state(Outbound::State::closed);
}

void OutboundUDP::start(double delay) {
  if (delay > 0) {
    m_retry_timer.schedule(
      delay,
      [this]() {
        resolve();
      }
    );
    state(State::idle);
  } else {
    resolve();
  }
}

void OutboundUDP::resolve() {
  static const std::string s_localhost("localhost");
  static const std::string s_localhost_ip("127.0.0.1");
  const auto &host = (m_host == s_localhost ? s_localhost_ip : m_host);

  m_resolver.async_resolve(
    udp::resolver::query(host, std::to_string(m_port)),
    [this](
      const std::error_code &ec,
      udp::resolver::results_type results
    ) {
      InputContext ic;

      if (ec && options().connect_timeout > 0) {
        m_connect_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[1000];
            describe(desc, sizeof(desc));
            Log::error("%s cannot resolve hostname: %s", desc, ec.message().c_str());
          }
          connect_error(StreamEnd::CANNOT_RESOLVE);

        } else if (state() == State::resolving) {
          auto &result = *results;
          const auto &target = result.endpoint();
          m_remote_addr = target.address().to_string();
          m_remote_addr_str = nullptr;
          connect(target);
        }
      }

      release();
    }
  );

  log_debug("resolving hostname...");

  if (options().connect_timeout > 0) {
    m_connect_timer.schedule(
      options().connect_timeout,
      [this]() {
        connect_error(StreamEnd::CONNECTION_TIMEOUT);
      }
    );
  }

  m_start_time = utils::now();

  if (m_retries > 0) {
    if (Log::is_enabled(Log::WARN)) {
      char desc[200];
      describe(desc, sizeof(desc));
      Log::warn("%s retry connecting... (retries = %d)", desc, m_retries);
    }
  }

  retain();
  state(State::resolving);
}

void OutboundUDP::connect(const asio::ip::udp::endpoint &target) {
  socket().async_connect(
    target,
    [=](const std::error_code &ec) {
      InputContext ic;

      if (options().connect_timeout > 0) {
        m_connect_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc, sizeof(desc));
            Log::error("%s cannot connect: %s", desc, ec.message().c_str());
          }
          connect_error(StreamEnd::CONNECTION_REFUSED);

        } else if (state() == State::connecting) {
          const auto &ep = socket().local_endpoint();
          m_local_addr = ep.address().to_string();
          m_local_port = ep.port();
          m_local_addr_str = nullptr;

          auto conn_time = utils::now() - m_start_time;
          m_connection_time += conn_time;
          m_metric_conn_time->observe(conn_time);
          s_metric_conn_time->observe(conn_time);

          if (Log::is_enabled(Log::OUTBOUND)) {
            char desc[200];
            describe(desc, sizeof(desc));
            Log::debug(Log::OUTBOUND, "%s connected in %g ms", desc, conn_time);
          }

          state(State::connected);
          retain();
          SocketUDP::open();
        }
      }

      release();
    }
  );

  if (Log::is_enabled(Log::OUTBOUND)) {
    char desc[200];
    describe(desc, sizeof(desc));
    Log::debug(Log::OUTBOUND, "%s connecting...", desc);
  }

  retain();
  state(State::connecting);
}

void OutboundUDP::connect_error(StreamEnd::Error err) {
  if (options().retry_count >= 0 && m_retries >= options().retry_count) {
    error(err);
  } else {
    m_retries++;
    std::error_code ec;
    socket().close(ec);
    m_resolver.cancel();
    state(State::idle);
    start(options().retry_delay);
  }
}

auto OutboundUDP::get_traffic_in() -> size_t {
  auto n = SocketUDP::m_traffic_read;
  SocketUDP::m_traffic_read = 0;
  return n;
}

auto OutboundUDP::get_traffic_out() -> size_t {
  auto n = SocketUDP::m_traffic_write;
  SocketUDP::m_traffic_write = 0;
  return n;
}

//
// OutboundNetlink
//

OutboundNetlink::OutboundNetlink(int family, EventTarget::Input *output, const Outbound::Options &options)
  : pjs::ObjectTemplate<OutboundNetlink, Outbound>(output, options)
  , SocketNetlink(false, Outbound::m_options)
  , m_family(family)
{
}

OutboundNetlink::~OutboundNetlink() {
  Outbound::collect();
}

void OutboundNetlink::bind(const std::string &address) {
#ifdef __linux__
  int pid = 0, groups = 0;
  to_nl_addr(address, pid, groups);
  auto &s = SocketNetlink::socket();
  sockaddr_nl addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = pid;
  addr.nl_groups = groups;
  asio::generic::raw_protocol::endpoint ep(&addr, sizeof(addr));
  s.open(asio::generic::raw_protocol(AF_NETLINK, m_family));
  s.bind(ep);
  m_local_addr = "localhost";
  m_local_port = 0;
  m_local_addr_str = nullptr;
#else // !__linux__
  throw std::runtime_error("netlink not supported on this platform");
#endif // __linux__
}

void OutboundNetlink::connect(const std::string &address) {
  int pid = 0, groups = 0;
  to_nl_addr(address, pid, groups);
  pjs::Str *keys[2];
  keys[0] = protocol_name();
  keys[1] = Outbound::address();
  m_metric_traffic_out = Outbound::s_metric_traffic_out->with_labels(keys, 2);
  m_metric_traffic_in = Outbound::s_metric_traffic_in->with_labels(keys, 2);

  state(State::connected);
  retain();
  SocketNetlink::open();
}

void OutboundNetlink::send(Event *evt) {
  SocketNetlink::output(evt);
}

void OutboundNetlink::close() {
  SocketNetlink::close();
  state(Outbound::State::closed);
}

auto OutboundNetlink::get_traffic_in() -> size_t {
  auto n = SocketNetlink::m_traffic_read;
  SocketNetlink::m_traffic_read = 0;
  return n;
}

auto OutboundNetlink::get_traffic_out() -> size_t {
  auto n = SocketNetlink::m_traffic_write;
  SocketNetlink::m_traffic_write = 0;
  return n;
}

void OutboundNetlink::to_nl_addr(const std::string &address, int &pid, int &groups) {
  utils::get_prop_list(
    address, ';', '=',
    [&](const std::string &k, const std::string &v) {
      if (k == "pid") {
        pid = std::atoi(v.c_str());
      } else if (k == "groups") {
        groups = std::atoi(v.c_str());
      } else {
        std::string msg("invalid address field for Netlink: ");
        throw std::runtime_error(msg + k);
      }
    }
  );
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void EnumDef<Outbound::Protocol>::init() {
  define(Outbound::Protocol::TCP, "tcp");
  define(Outbound::Protocol::UDP, "udp");
  define(Outbound::Protocol::NETLINK, "netlink");
}

template<> void EnumDef<Outbound::State>::init() {
  define(Outbound::State::idle, "idle");
  define(Outbound::State::resolving, "resolving");
  define(Outbound::State::connecting, "connecting");
  define(Outbound::State::connected, "connected");
  define(Outbound::State::closed, "closed");
}

template<> void ClassDef<Outbound>::init() {
  accessor("state",         [](Object *obj, Value &ret) { ret.set(EnumDef<Outbound::State>::name(obj->as<Outbound>()->state())); });
  accessor("localAddress" , [](Object *obj, Value &ret) { ret.set(obj->as<Outbound>()->local_address()); });
  accessor("localPort"    , [](Object *obj, Value &ret) { ret.set(obj->as<Outbound>()->local_port()); });
  accessor("remoteAddress", [](Object *obj, Value &ret) { ret.set(obj->as<Outbound>()->remote_address()); });
  accessor("remotePort"   , [](Object *obj, Value &ret) { ret.set(obj->as<Outbound>()->remote_port()); });

  method("close", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Outbound>()->close(StreamEnd::make(StreamEnd::CONNECTION_ABORTED));
  });
}

template<> void ClassDef<OutboundTCP>::init() {
  super<Outbound>();
}

template<> void ClassDef<OutboundUDP>::init() {
  super<Outbound>();
}

template<> void ClassDef<OutboundNetlink>::init() {
  super<Outbound>();
}

} // namespace pjs
