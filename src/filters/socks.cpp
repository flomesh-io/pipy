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
#include "log.hpp"

namespace pipy {
namespace socks {

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
  m_pipeline = nullptr;
  m_state = READ_VERSION;
}

void Server::process(Event *evt) {
  static Data::Producer s_dp("acceptSOCKS");

  auto reply = [this](const uint8_t *buf, size_t len) {
    output(s_dp.make(buf, len));
  };

  auto reply_socks4 = [this](int rep) {
    uint8_t buf[8] = { 0 };
    buf[1] = rep;
    output(s_dp.make(buf, sizeof(buf)));
  };

  auto reply_socks5 = [this](int rep) {
    uint8_t buf[10] = { 0 };
    buf[0] = 0x05;
    buf[1] = rep;
    buf[3] = 0x01;
    output(s_dp.make(buf, sizeof(buf)));
  };

  auto close = [this](Event *evt) {
    m_pipeline = nullptr;
    output(evt);
  };

  auto connect = [&](int version) {
    pjs::Value argv[3], ret;
    if (m_domain[0]) {
      argv[0].set(pjs::Str::make((char*)m_domain));
    } else {
      char addr[100];
      std::sprintf(addr, "%d.%d.%d.%d", m_ip[0], m_ip[1], m_ip[2], m_ip[3]);
      argv[0].set(addr);
    }
    int port = (int(m_port[0]) << 8) + m_port[1];
    argv[1].set(port);
    if (m_id[0]) {
      argv[2].set(pjs::Str::make((char*)m_id));
    }
    callback(m_on_connect, 3, argv, ret);
    if (ret.to_boolean()) {
      auto pipeline = sub_pipeline(0, false, output());
      m_pipeline = pipeline;
      if (version == 4) {
        reply_socks4(0x5a);
      } else {
        reply_socks5(0x00);
      }
      return;
    }
    if (version == 4) {
      reply_socks4(0x5b);
    } else {
      reply_socks5(0x02);
    }
    close(StreamEnd::make());
  };

  if (auto *data = evt->as<Data>()) {
    if (m_pipeline) {
      output(data, m_pipeline->input());

    } else {
      Data parsed;
      data->shift_to(
        [&](int c) -> bool {
          switch (m_state) {
          case READ_VERSION:
            m_id[0] = 0;
            m_domain[0] = 0;
            if (c == 4) {
              m_state = READ_SOCKS4_CMD;
            } else if (c == 5) {
              m_state = READ_SOCKS5_NAUTH;
            } else {
              close(StreamEnd::make());
              return true;
            }
            break;

          // SOCKS4
          case READ_SOCKS4_CMD:
            if (c == 0x01) {
              m_state = READ_SOCKS4_DSTPORT;
              m_read_ptr = 0;
            } else {
              reply_socks4(0x5b);
              close(StreamEnd::make());
              return true;
            }
            break;
          case READ_SOCKS4_DSTPORT:
            m_port[m_read_ptr++] = c;
            if (m_read_ptr == 2) {
              m_state = READ_SOCKS4_DSTIP;
              m_read_ptr = 0;
            }
            break;
          case READ_SOCKS4_DSTIP:
            m_ip[m_read_ptr++] = c;
            if (m_read_ptr == 4) {
              m_state = READ_SOCKS4_ID;
              m_read_ptr = 0;
            }
            break;
          case READ_SOCKS4_ID:
            if (c) {
              if (m_read_ptr < sizeof(m_id)-1) {
                m_id[m_read_ptr++] = c;
              } else {
                reply_socks4(0x5b);
                close(StreamEnd::make());
                return true;
              }
            } else {
              m_id[m_read_ptr] = 0;
              if (!m_ip[0] && !m_ip[1] && !m_ip[2]) {
                m_state = READ_SOCKS4_DOMAIN;
                m_read_ptr = 0;
              } else {
                connect(4);
                return true;
              }
            }
            break;
          case READ_SOCKS4_DOMAIN:
            if (c) {
              if (m_read_ptr < sizeof(m_domain)-1) {
                m_domain[m_read_ptr++] = c;
              } else {
                reply_socks4(0x5b);
                close(StreamEnd::make());
                return true;
              }
            } else {
              m_domain[m_read_ptr] = 0;
              connect(4);
              return true;
            }
            break;

          // SOCKS5
          case READ_SOCKS5_NAUTH:
            m_read_len = c;
            m_read_ptr = 0;
            m_state = READ_SOCKS5_AUTH;
            break;
          case READ_SOCKS5_AUTH:
            if (++m_read_ptr == m_read_len) {
              uint8_t buf[2];
              buf[0] = 0x05;
              buf[1] = 0x00;
              reply(buf, sizeof(buf));
              m_state = READ_SOCKS5_CMD;
              m_read_ptr = 0;
            }
            break;
          case READ_SOCKS5_CMD:
            if ((m_read_ptr == 0 && c != 0x05) ||
                (m_read_ptr == 1 && c != 0x01) ||
                (m_read_ptr == 2 && c != 0x00))
            {
              reply_socks5(0x01);
              close(StreamEnd::make());
              return true;
            } else if (++m_read_ptr == 3) {
              m_state = READ_SOCKS5_ADDR_TYPE;
            }
            break;
          case READ_SOCKS5_ADDR_TYPE:
            if (c == 0x01) {
              m_state = READ_SOCKS5_DSTIP;
              m_read_ptr = 0;
            } else if (c == 0x3) {
              m_state = READ_SOCKS5_DOMAIN_LEN;
            } else {
              reply_socks5(0x08);
              close(StreamEnd::make());
              return true;
            }
            break;
          case READ_SOCKS5_DOMAIN_LEN:
            m_read_len = c & 0xff;
            m_read_ptr = 0;
            m_state = READ_SOCKS5_DOMAIN;
            break;
          case READ_SOCKS5_DOMAIN:
            m_domain[m_read_ptr++] = c;
            if (m_read_ptr == m_read_len) {
              m_domain[m_read_ptr] = 0;
              m_state = READ_SOCKS5_DSTPORT;
              m_read_ptr = 0;
            }
            break;
          case READ_SOCKS5_DSTIP:
            m_ip[m_read_ptr++] = c;
            if (m_read_ptr == 4) {
              m_state = READ_SOCKS5_DSTPORT;
              m_read_ptr = 0;
            }
            break;
          case READ_SOCKS5_DSTPORT:
            m_port[m_read_ptr++] = c;
            if (m_read_ptr == 2) {
              connect(5);
              return true;
            }
            break;
          }
          return false;
        },
        parsed
      );

      if (m_pipeline && !data->empty()) {
        output(data, m_pipeline->input());
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (m_pipeline) {
      output(evt, m_pipeline->input());
    }
    close(evt);
  }
}

//
// Client
//

static Data::Producer s_dp("connectSOCKS");

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
    m_pipeline = sub_pipeline(0, false, ClientReceiver::input());
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
        Log::warn("[connectSOCKS] domain name too long: %s", host.c_str());
        host = host.substr(0, 255);
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
    Log::error("[connectSOCKS] invalid target: %s", s->c_str());
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
