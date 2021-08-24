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
#include "session.hpp"
#include "module.hpp"
#include "logging.hpp"

namespace pipy {

//
// ProxySOCKS
//

ProxySOCKS::ProxySOCKS()
{
}

ProxySOCKS::ProxySOCKS(pjs::Str *target, pjs::Function *on_connect)
  : m_target(target)
  , m_on_connect(on_connect)
{
}

ProxySOCKS::ProxySOCKS(const ProxySOCKS &r)
  : m_pipeline(r.m_pipeline)
  , m_target(r.m_target)
  , m_on_connect(r.m_on_connect)
{
}

ProxySOCKS::~ProxySOCKS()
{
}

auto ProxySOCKS::help() -> std::list<std::string> {
  return {
    "proxySOCKS(target, onConnect)",
    "Proxies a SOCKS connection to a different pipeline",
    "target = <string> Name of the pipeline that receives SOCKS connections",
    "onConnect = <function> Callback function that receives address, port, user and returns whether the connection is accepted",
  };
}

void ProxySOCKS::dump(std::ostream &out) {
  out << "proxySOCKS";
}

auto ProxySOCKS::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = false;
  return "proxySOCKS";
}

void ProxySOCKS::bind() {
  if (!m_pipeline) {
    m_pipeline = pipeline(m_target);
  }
}

auto ProxySOCKS::clone() -> Filter* {
  return new ProxySOCKS(*this);
}

void ProxySOCKS::reset() {
  m_session = nullptr;
  m_state = READ_VERSION;
  m_session_end = false;
}

void ProxySOCKS::process(Context *ctx, Event *inp) {
  static Data::Producer s_dp("proxySOCKS");

  if (m_session_end) return;

  auto reply = [this](const uint8_t *buf, size_t len) {
    output(s_dp.make(buf, len));
    output(Data::flush());
  };

  auto reply_socks4 = [this](int rep) {
    uint8_t buf[8] = { 0 };
    buf[1] = rep;
    output(s_dp.make(buf, sizeof(buf)));
    output(Data::flush());
  };

  auto reply_socks5 = [this](int rep) {
    uint8_t buf[10] = { 0 };
    buf[0] = 0x05;
    buf[1] = rep;
    buf[3] = 0x01;
    output(s_dp.make(buf, sizeof(buf)));
    output(Data::flush());
  };

  auto close = [this](Event *inp) {
    m_session = nullptr;
    m_session_end = true;
    output(inp);
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
    callback(*ctx, m_on_connect, 3, argv, ret);
    if (ret.to_boolean()) {
      auto root = static_cast<Context*>(ctx->root());
      auto session = Session::make(root, m_pipeline);
      session->on_output(out());
      m_session = session;
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
    close(SessionEnd::make());
  };

  if (auto *data = inp->as<Data>()) {
    if (m_session) {
      m_session->input(inp);

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
              close(SessionEnd::make());
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
              close(SessionEnd::make());
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
                close(SessionEnd::make());
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
                close(SessionEnd::make());
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
              close(SessionEnd::make());
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
              close(SessionEnd::make());
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

      if (m_session && !data->empty()) {
        m_session->input(data);
      }
    }

  } else if (inp->is<SessionEnd>()) {
    if (m_session) {
      m_session->input(inp);
    }
    close(inp);
  }
}

} // namespace pipy