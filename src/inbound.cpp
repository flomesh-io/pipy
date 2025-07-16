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

#include "inbound.hpp"
#include "listener.hpp"
#include "pipeline.hpp"
#include "worker.hpp"
#include "constants.hpp"
#include "log.hpp"

#ifdef __linux__
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#endif // __linux__

namespace pipy {

using tcp = asio::ip::tcp;

//
// Inbound
//

static Data::Producer s_dp_tcp("InboundTCP");
static Data::Producer s_dp_udp("InboundUDP Raw");

std::atomic<uint64_t> Inbound::s_inbound_id;

thread_local pjs::Ref<stats::Gauge> Inbound::s_metric_concurrency;
thread_local pjs::Ref<stats::Counter> Inbound::s_metric_traffic_in;
thread_local pjs::Ref<stats::Counter> Inbound::s_metric_traffic_out;

auto Inbound::count() -> int {
  int n = 0;
  Listener::for_each(
    [&](Listener *l) {
      n += l->current_connections();
      return true;
    }
  );
  return n;
}

void Inbound::for_each(const std::function<bool(Inbound*)> &cb) {
  Listener::for_each(
    [&](Listener *l) {
      return l->for_each_inbound(cb);
    }
  );
}

Inbound::Inbound(Listener *listener, const Options &options)
  : m_listener(listener)
  , m_options(options)
{
  init_metrics();
  Log::debug(Log::ALLOC, "[inbound  %p] ++", this);
  for (;;) {
    if (auto id = s_inbound_id.fetch_add(1, std::memory_order_relaxed) + 1) {
      m_id = id;
      break;
    }
  }
}

Inbound::~Inbound() {
  Log::debug(Log::ALLOC, "[inbound  %p] --", this);
}

auto Inbound::local_address() -> pjs::Str* {
  if (!m_str_local_addr) {
    address();
    m_str_local_addr = pjs::Str::make(m_local_addr);
  }
  return m_str_local_addr;
}

auto Inbound::remote_address() -> pjs::Str* {
  if (!m_str_remote_addr) {
    address();
    m_str_remote_addr = pjs::Str::make(m_remote_addr);
  }
  return m_str_remote_addr;
}

auto Inbound::ori_dst_address() -> pjs::Str* {
  if (!m_str_ori_dst_addr) {
    address();
    m_str_ori_dst_addr = pjs::Str::make(m_ori_dst_addr);
  }
  return m_str_ori_dst_addr;
}

void Inbound::start() {
  if (!m_pipeline) {
    auto layout = m_listener->pipeline_layout();
    auto ctx = layout->new_context();
    ctx->m_inbound = this;
    auto p = Pipeline::make(layout, ctx);
    m_pipeline = p;

    p->chain(EventTarget::input());
    m_input = p->input();
    m_listener->open(this);

    int n = 1;
    pjs::Str *labels[2];
    labels[0] = m_listener->label();
    if (m_options.peer_stats) labels[n++] = remote_address();

    m_metric_traffic_in = Inbound::s_metric_traffic_in->with_labels(labels, n);
    m_metric_traffic_out = Inbound::s_metric_traffic_out->with_labels(labels, n);

    pjs::Value arg(InboundWrapper::make(this));
    p->start(1, &arg);
  }
}

void Inbound::restart() {
  m_listener->accept();
  m_listener = nullptr;
}

void Inbound::end() {
  if (m_listener) m_listener->close(this);
  m_pipeline = nullptr;
}

void Inbound::collect() {
  auto in = get_traffic_in();
  auto out = get_traffic_out();
  s_metric_traffic_in->increase(in);
  s_metric_traffic_out->increase(out);
  if (m_metric_traffic_in) m_metric_traffic_in->increase(in);
  if (m_metric_traffic_out) m_metric_traffic_out->increase(out);
}

void Inbound::address() {
  if (!m_addressed) {
    on_get_address();
    m_addressed = true;
  }
}

void Inbound::init_metrics() {
  if (!s_metric_concurrency) {
    pjs::Ref<pjs::Array> label_names = pjs::Array::make();
    label_names->length(2);
    label_names->set(0, "listen");
    label_names->set(1, "peer");

    s_metric_concurrency = stats::Gauge::make(
      pjs::Str::make("pipy_inbound_count"),
      label_names,
      [=](stats::Gauge *gauge) {
        int total = 0;
        Listener::for_each([&](Listener *listener) {
          if (listener->is_open()) {
            auto k = listener->label();
            auto l = gauge->with_labels(&k, 1);
            if (listener->options().peer_stats) {
              auto n = 0;
              l->zero_all();
              listener->for_each_inbound([&](Inbound *inbound) {
                auto k = inbound->remote_address();
                auto m = l->with_labels(&k, 1);
                m->increase();
                n++;
                return true;
              });
              l->set(n);
              total += n;
            } else {
              auto n = listener->current_connections();
              l->set(n);
              total += n;
            }
          }
          return true;
        });
        gauge->set(total);
      }
    );

    s_metric_traffic_in = stats::Counter::make(
      pjs::Str::make("pipy_inbound_in"),
      label_names,
      [](stats::Counter *counter) {
        Listener::for_each([&](Listener *listener) {
          listener->for_each_inbound([&](Inbound *inbound) {
            auto n = inbound->get_traffic_in();
            inbound->m_metric_traffic_in->increase(n);
            s_metric_traffic_in->increase(n);
            return true;
          });
          return true;
        });
      }
    );

    s_metric_traffic_out = stats::Counter::make(
      pjs::Str::make("pipy_inbound_out"),
      label_names,
      [](stats::Counter *counter) {
        Listener::for_each([&](Listener *listener) {
          listener->for_each_inbound([&](Inbound *inbound) {
            auto n = inbound->get_traffic_out();
            inbound->m_metric_traffic_out->increase(n);
            s_metric_traffic_out->increase(n);
            return true;
          });
          return true;
        });
      }
    );
  }
}

//
// InboundTCP
//

InboundTCP::InboundTCP(Listener *listener, const Inbound::Options &options)
  : pjs::ObjectTemplate<InboundTCP, Inbound>(listener, options)
  , SocketTCP(true, options)
{
}

InboundTCP::~InboundTCP() {
  if (m_socket) {
    m_socket->discard();
  }
  collect();
}

void InboundTCP::accept(asio::ip::tcp::acceptor &acceptor) {
  acceptor.async_accept(
    socket(), m_peer,
    [this](const std::error_code &ec) {
      InputContext ic(this);

      if (ec == asio::error::operation_aborted) {
        dangle();
      } else if (!m_canceled) {
        if (ec) {
          log_error("error accepting connection", ec);
          restart();

        } else if (m_listener && m_listener->pipeline_layout()) {
          log_debug("connection accepted");
          start();
        }
      }

      release();
    }
  );

  retain();
}

auto InboundTCP::get_socket() -> Socket* {
  if (!m_socket) {
    m_socket = Socket::make(this, SocketTCP::socket().native_handle());
  }
  return m_socket;
}

auto InboundTCP::get_traffic_in() -> size_t {
  auto n = SocketTCP::m_traffic_read;
  SocketTCP::m_traffic_read = 0;
  return n;
}

auto InboundTCP::get_traffic_out() -> size_t {
  auto n = SocketTCP::m_traffic_write;
  SocketTCP::m_traffic_write = 0;
  return n;
}

void InboundTCP::on_get_address() {
  auto &s = SocketTCP::socket();
  if (s.is_open()) {
    const auto &ep = s.local_endpoint();
    m_local_addr = ep.address().to_string();
    m_local_port = ep.port();
  }

  m_remote_addr = m_peer.address().to_string();
  m_remote_port = m_peer.port();

#ifdef __linux__
  if (Inbound::m_options.transparent && s.is_open()) {
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    if (!getsockopt(s.native_handle(), SOL_IP, SO_ORIGINAL_DST, &addr, &len)) {
      char str[100];
      auto n = std::sprintf(
        str, "%d.%d.%d.%d",
        (unsigned char)addr.sa_data[2],
        (unsigned char)addr.sa_data[3],
        (unsigned char)addr.sa_data[4],
        (unsigned char)addr.sa_data[5]
      );
      m_ori_dst_addr.assign(str, n);
      m_ori_dst_port = (
        ((int)(unsigned char)addr.sa_data[0] << 8) |
        ((int)(unsigned char)addr.sa_data[1] << 0)
      );
    }
  }
#endif // __linux__
}

void InboundTCP::start() {
  Inbound::start();
  retain();
  SocketTCP::open();
}

void InboundTCP::describe(char *buf, size_t len) {
  address();
  if (Inbound::m_options.transparent) {
    std::snprintf(
      buf, len,
      "[inbound] [%s]:%d -> [%s]:%d -> [%s]:%d",
      m_remote_addr.c_str(),
      m_remote_port,
      m_local_addr.c_str(),
      m_local_port,
      m_ori_dst_addr.c_str(),
      m_ori_dst_port
    );
  } else {
    std::snprintf(
      buf, len,
      "[inbound] [%s]:%d -> [%s]:%d",
      m_remote_addr.c_str(),
      m_remote_port,
      m_local_addr.c_str(),
      m_local_port
    );
  }
}

//
// InboundUDP
//

InboundUDP::InboundUDP(Listener* listener, const Options &options, Socket *socket)
  : pjs::ObjectTemplate<InboundUDP, Inbound>(listener, options)
  , m_socket(socket)
{
}

InboundUDP::~InboundUDP() {
  Inbound::collect();
}

auto InboundUDP::get_socket() -> Socket* {
  return m_socket;
}

auto InboundUDP::get_buffered() const -> size_t {
  return 0;
}

auto InboundUDP::get_traffic_in() -> size_t {
  return 0;
}

auto InboundUDP::get_traffic_out() -> size_t {
  return 0;
}

void InboundUDP::on_get_address() {
  if (m_listener) {
    const auto &local = SocketUDP::Peer::local();
    const auto &peer = SocketUDP::Peer::peer();
    m_local_addr = local.address().to_string();
    m_local_port = local.port();
    m_remote_addr = peer.address().to_string();
    m_remote_port = peer.port();
  }
}

void InboundUDP::on_event(Event *evt) {
  SocketUDP::Peer::output(evt);
}

void InboundUDP::on_peer_open() {
  Inbound::retain();
  Inbound::start();
}

void InboundUDP::on_peer_input(Event *evt) {
  m_input->input(evt);
}

void InboundUDP::on_peer_close() {
  Inbound::end();
  Inbound::release();
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Inbound>::init() {
  accessor("id"                 , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->id()); });
  accessor("localAddress"       , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->local_address()); });
  accessor("localPort"          , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->local_port()); });
  accessor("remoteAddress"      , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->remote_address()); });
  accessor("remotePort"         , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->remote_port()); });
  accessor("destinationAddress" , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->ori_dst_address()); });
  accessor("destinationPort"    , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->ori_dst_port()); });
  accessor("socket"             , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->get_socket()); });
}

template<> void ClassDef<InboundTCP>::init() {
  super<Inbound>();
}

template<> void ClassDef<InboundUDP>::init() {
  super<Inbound>();
}

template<> void ClassDef<InboundWrapper>::init() {
  accessor("id"                 , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->id()); });
  accessor("localAddress"       , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->local_address()); });
  accessor("localPort"          , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->local_port()); });
  accessor("remoteAddress"      , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->remote_address()); });
  accessor("remotePort"         , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->remote_port()); });
  accessor("destinationAddress" , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->ori_dst_address()); });
  accessor("destinationPort"    , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->ori_dst_port()); });
  accessor("socket"             , [](Object *obj, Value &ret) { if (auto i = obj->as<InboundWrapper>()->get()) ret.set(i->get_socket()); });
}

} // namespace pjs
