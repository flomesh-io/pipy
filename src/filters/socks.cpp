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

#include "socks.hpp"
#include "data.hpp"
#include "pipeline.hpp"
#include "module.hpp"

namespace pipy {
namespace socks {

thread_local static Data::Producer s_dp("SOCKS");

//
// Server
//

Server::Server(pjs::Function *on_connect)
  : m_on_connect(on_connect)
{
}

Server::Server(const Server &r)
  : Filter(r)
  , m_on_connect(r.m_on_connect)
{
}

Server::~Server()
{
}

void Server::dump(Dump &d) {
  Filter::dump(d);
  d.name = "acceptSOCKS";
}

auto Server::clone() -> Filter* {
  return new Server(*this);
}

void Server::reset() {
  Filter::reset();
  Deframer::reset(READ_VERSION);
  Deframer::pass_all(false);
  m_pipeline = nullptr;
  m_id = nullptr;
  m_domain = nullptr;
  m_port = 0;
  m_read_ptr = 0;
}

void Server::process(Event *evt) {
  if (m_pipeline) {
    Filter::output(evt, m_pipeline->input());
  } else if (auto data = evt->as<Data>()) {
    Deframer::deframe(*data);
  } else if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  }
}

auto Server::on_state(int state, int c) -> int {
  switch (state) {
    case READ_VERSION:
      if (c == 4) {
        return READ_SOCKS4_CMD;
      } else if (c == 5) {
        return READ_SOCKS5_NAUTH;
      }
      break;

    // SOCKS4
    case READ_SOCKS4_CMD:
      if (c == 0x01) {
        Deframer::read(2, m_buffer);
        return READ_SOCKS4_DSTPORT;
      }
      reply(4, 0x5b);
      break;
    case READ_SOCKS4_DSTPORT:
      m_port = (
        ((uint16_t)m_buffer[0] << 8)|
        ((uint16_t)m_buffer[1] << 0)
      );
      Deframer::read(4, m_ip);
      return READ_SOCKS4_DSTIP;
    case READ_SOCKS4_DSTIP: {
      m_read_ptr = 0;
      return READ_SOCKS4_ID;
    }
    case READ_SOCKS4_ID:
      if (c) {
        if (m_read_ptr < sizeof(m_buffer)-1) {
          m_buffer[m_read_ptr++] = c;
          return READ_SOCKS4_ID;
        } else {
          reply(4, 0x5b);
        }
      } else {
        m_id = pjs::Str::make((char*)m_buffer, m_read_ptr);
        if (!m_ip[0] && !m_ip[1] && !m_ip[2]) {
          m_read_ptr = 0;
          return READ_SOCKS4_DOMAIN;
        } else if (start(4)) {
          Deframer::pass_all(true);
          return STARTED;
        }
      }
      break;
    case READ_SOCKS4_DOMAIN:
      if (c) {
        if (m_read_ptr < sizeof(m_buffer)-1) {
          m_buffer[m_read_ptr++] = c;
          return READ_SOCKS4_DOMAIN;
        } else {
          reply(4, 0x5b);
        }
      } else {
        m_domain = pjs::Str::make((char*)m_buffer, m_read_ptr);
        if (start(4)) {
          Deframer::pass_all(true);
          return STARTED;
        }
      }
      break;

    // SOCKS5
    case READ_SOCKS5_NAUTH:
      Deframer::read(c, m_id);
      return READ_SOCKS5_AUTH;
    case READ_SOCKS5_AUTH:
      uint8_t buf[2];
      buf[0] = 0x05;
      buf[1] = 0x00;
      Filter::output(s_dp.make(buf, sizeof(buf)));
      Deframer::read(3, m_buffer);
      return READ_SOCKS5_CMD;
    case READ_SOCKS5_CMD:
      if (
        m_buffer[0] == 0x05 &&
        m_buffer[1] == 0x01 &&
        m_buffer[2] == 0x00
      ) {
        return READ_SOCKS5_ADDR_TYPE;
      } else {
        reply(5, 0x01);
      }
      break;
    case READ_SOCKS5_ADDR_TYPE:
      if (c == 0x01) {
        Deframer::read(4, m_ip);
        return READ_SOCKS5_DSTIP;
      } else if (c == 0x03) {
        return READ_SOCKS5_DOMAIN_LEN;
      } else {
        reply(5, 0x08);
      }
      break;
    case READ_SOCKS5_DOMAIN_LEN:
      m_read_ptr = c;
      Deframer::read(c, m_buffer);
      return READ_SOCKS5_DOMAIN;
    case READ_SOCKS5_DOMAIN:
      m_domain = pjs::Str::make((char*)m_buffer, m_read_ptr);
      Deframer::read(2, m_buffer);
      return READ_SOCKS5_DSTPORT;
    case READ_SOCKS5_DSTIP:
      Deframer::read(2, m_buffer);
      return READ_SOCKS5_DSTPORT;
    case READ_SOCKS5_DSTPORT:
      m_port = (
        ((uint16_t)m_buffer[0] << 8)|
        ((uint16_t)m_buffer[1] << 0)
      );
      if (start(5)) {
        Deframer::pass_all(true);
        return STARTED;
      }
      break;
  }

  Filter::output(StreamEnd::make());
  m_pipeline = nullptr;
  return -1;
}

void Server::on_pass(Data &data) {
  if (m_pipeline) {
    Filter::output(Data::make(std::move(data)), m_pipeline->input());
  }
}

bool Server::start(int version) {
  auto req = Request::make();
  req->id = m_id;
  req->port = m_port;

  if (m_domain) {
    req->domain = m_domain;
  } else {
    char str[100];
    auto len = std::snprintf(str, sizeof(str), "%d.%d.%d.%d", m_ip[0], m_ip[1], m_ip[2], m_ip[3]);
    req->ip = pjs::Str::make(str, len);
  }

  pjs::Value arg(req), ret;
  if (!Filter::callback(m_on_connect, 1, &arg, ret)) return false;
  if (!ret.to_boolean()) {
    reply(version, (version == 4 ? 0x5b : 0x02));
    return false;
  }

  reply(version, (version == 4 ? 0x5a : 0x00));
  m_pipeline = Filter::sub_pipeline(0, false, Filter::output())->start();
  return true;
}

void Server::reply(int version, int code) {
  if (version == 4) {
    uint8_t buf[8] = { 0 };
    buf[1] = code;
    Filter::output(s_dp.make(buf, sizeof(buf)));
  } else {
    uint8_t buf[10] = { 0 };
    buf[0] = 0x05;
    buf[1] = code;
    buf[3] = 0x01;
    Filter::output(s_dp.make(buf, sizeof(buf)));
  }
}

//
// Client
//

void ClientReceiver::on_event(Event *evt) {
  static_cast<Client*>(this)->on_receive(evt);
}

Client::Client(const pjs::Value &target)
  : m_target(target)
{
}

Client::Client(const Client &r)
  : Filter(r)
  , m_target(r.m_target)
{
}

Client::~Client()
{
}

void Client::dump(Dump &d) {
  Filter::dump(d);
  d.name = "connectSOCKS";
}

auto Client::clone() -> Filter* {
  return new Client(*this);
}

void Client::reset() {
  Filter::reset();
  m_pipeline = nullptr;
  m_state = STATE_INIT;
  m_read_size = 0;
  m_buffer.clear();
}

void Client::process(Event *evt) {
  if (m_state == STATE_CLOSED) return;

  if (!m_pipeline) {
    m_pipeline = sub_pipeline(0, false, ClientReceiver::input())->start();
  }

  if (evt->is<StreamEnd>()) {
    output(evt, m_pipeline->input());
    return;
  }

  if (auto data = evt->as<Data>()) {
    if (m_state == STATE_CONNECTED) {
      if (!data->empty()) send(data);
      return;
    } else {
      m_buffer.push(*data);
    }
  }

  if (m_state == STATE_INIT) {
    uint8_t greeting[] = { 0x05, 1, 0 };
    send(greeting, sizeof(greeting));
    m_state = STATE_READ_AUTH;
  }
}

void Client::on_receive(Event *evt) {
  if (m_state == STATE_CLOSED) return;

  if (evt->is<StreamEnd>()) {
    m_state = STATE_CLOSED;
    m_pipeline = nullptr;
    output(evt);
    return;
  }

  if (auto data = evt->as<Data>()) {
    if (m_state == STATE_CONNECTED) {
      output(data);
      return;
    }

    Data parsed;
    data->shift_to(
      [&](int c) -> bool {
        switch (m_state) {
          case STATE_READ_AUTH: {
            m_read_buffer[m_read_size++] = c;
            if (m_read_size == 2) {
              if (
                m_read_buffer[0] != 0x05 ||
                m_read_buffer[1] != 0x00
              ) {
                close(StreamEnd::UNAUTHORIZED);
                return true;
              }
              connect();
              m_state = STATE_READ_CONN_HEAD;
              m_read_size = 0;
            }
            break;
          }
          case STATE_READ_CONN_HEAD: {
            m_read_buffer[m_read_size++] = c;
            if (m_read_size == 3) {
              if (
                m_read_buffer[0] != 0x05 ||
                m_read_buffer[1] != 0x00 ||
                m_read_buffer[2] != 0x00
              ) {
                close(StreamEnd::CONNECTION_REFUSED);
                return true;
              }
              m_state = STATE_READ_CONN_ADDR;
              m_read_size = 0;
            }
            break;
          }
          case STATE_READ_CONN_ADDR: {
            m_read_buffer[m_read_size++] = c;
            if (m_read_size == 2) {
              switch (m_read_buffer[0]) {
                case 0x01: m_state = STATE_READ_CONN_ADDR_IPV4; break;
                case 0x04: m_state = STATE_READ_CONN_ADDR_IPV6; break;
                case 0x03: m_state = STATE_READ_CONN_ADDR_DOMAIN; break;
                default: close(StreamEnd::READ_ERROR); return true;
              }
            }
            break;
          }
          case STATE_READ_CONN_ADDR_IPV4: {
            if (++m_read_size == 1 + 4 + 2) {
              m_state = STATE_CONNECTED;
              return true;
            }
            break;
          }
          case STATE_READ_CONN_ADDR_IPV6: {
            if (++m_read_size == 1 + 16 + 2) {
              m_state = STATE_CONNECTED;
              return true;
            }
            break;
          }
          case STATE_READ_CONN_ADDR_DOMAIN: {
            if (++m_read_size == 2 + m_read_buffer[1] + 2) {
              m_state = STATE_CONNECTED;
              return true;
            }
            break;
          }
          default: break;
        }
        return false;
      },
      parsed
    );

    if (m_state == STATE_CONNECTED) {
      if (!m_buffer.empty()) {
        send(Data::make(m_buffer));
        m_buffer.clear();
      }
      if (!data->empty()) output(data);
    }
  }
}

void Client::send(Data *data) {
  auto inp = m_pipeline->input();
  output(data, inp);
}

void Client::send(const uint8_t *buf, size_t len) {
  send(s_dp.make(buf, len));
}

void Client::connect() {
  pjs::Value target;
  if (!eval(m_target, target)) return;
  auto s = target.to_string();
  std::string host; int port;
  if (utils::get_host_port(s->str(), host, port)) {
    uint8_t buf[300];
    buf[0] = 0x05;
    buf[1] = 0x01;
    buf[2] = 0x00;
    size_t len = 0;
    if (utils::get_ip_v4(host, buf + 4)) {
      buf[3] = 0x01;
      buf[8] = (port >> 8) & 0xff;
      buf[9] = (port >> 0) & 0xff;
      len = 4 + 4 + 2;
    } else if (utils::get_ip_v6(host, buf + 4)) {
      buf[3] = 0x04;
      buf[20] = (port >> 8) & 0xff;
      buf[21] = (port >> 0) & 0xff;
      len = 4 + 16 + 2;
    } else {
      if (host.length() > 255) {
        Filter::error("domain name too long: %s", host.c_str());
        s->release();
        return;
      }
      auto n = host.length();
      buf[3] = 0x03;
      buf[4] = n;
      std::memcpy(buf + 5, host.c_str(), n);
      buf[5 + n] = (port >> 8) & 0xff;
      buf[6 + n] = (port >> 0) & 0xff;
      len = 5 + n + 2;
    }
    send(buf, len);
    m_state = STATE_READ_CONN_HEAD;
  } else {
    Filter::error("invalid target: %s", s->c_str());
  }
  s->release();
}

void Client::close(StreamEnd::Error err) {
  m_state = STATE_CLOSED;
  m_pipeline = nullptr;
  output(StreamEnd::make(err));
}

} // namespace socks
} // namespace pipy

namespace pjs {

using namespace pipy::socks;

template<> void ClassDef<Server::Request>::init() {
  field<pjs::Ref<pjs::Str>>("id", [](Server::Request *obj) { return &obj->id; });
  field<pjs::Ref<pjs::Str>>("ip", [](Server::Request *obj) { return &obj->ip; });
  field<pjs::Ref<pjs::Str>>("domain", [](Server::Request *obj) { return &obj->domain; });
  field<int>("port", [](Server::Request *obj) { return &obj->port; });
}

} // namespace pjs
