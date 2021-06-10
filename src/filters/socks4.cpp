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

#include "socks4.hpp"
#include "data.hpp"
#include "session.hpp"
#include "module.hpp"
#include "logging.hpp"

namespace pipy {

//
// ProxySOCKS4
//

ProxySOCKS4::ProxySOCKS4()
{
}

ProxySOCKS4::ProxySOCKS4(pjs::Str *target, pjs::Function *on_connect)
  : m_target(target)
  , m_on_connect(on_connect)
{
}

ProxySOCKS4::ProxySOCKS4(const ProxySOCKS4 &r)
  : ProxySOCKS4()
{
}

ProxySOCKS4::~ProxySOCKS4()
{
}

auto ProxySOCKS4::help() -> std::list<std::string> {
  return {
    "proxySOCKS4(target, onConnect)",
    "Proxies a SOCKS4 connection to a different pipeline",
    "target = <string> Name of the pipeline that receives SOCKS4 connections",
    "onConnect = <function> Callback function that receives address, port, user and returns whether the connection is accepted",
  };
}

void ProxySOCKS4::dump(std::ostream &out) {
  out << "proxySOCKS4";
}

auto ProxySOCKS4::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = false;
  return "proxySOCKS4";
}

auto ProxySOCKS4::clone() -> Filter* {
  return new ProxySOCKS4(m_target, m_on_connect);
}

void ProxySOCKS4::reset() {
  m_session = nullptr;
  m_state = READ_COMMAND;
  m_command_read_ptr = 0;
  m_user_id_read_ptr = 0;
  m_domain_read_ptr = 0;
  m_session_end = false;
}

void ProxySOCKS4::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  auto reply = [this](int rep) {
    uint8_t buf[8] = { 0 };
    buf[1] = rep;
    output(Data::make(buf, sizeof(buf)));
    output(Data::make()); // flush
  };

  auto close = [this](Event *inp) {
    m_session = nullptr;
    m_session_end = true;
    output(inp);
  };

  auto connect = [&]() {
    pjs::Value argv[3], ret;
    if (m_domain_read_ptr > 0) {
      argv[0].set(pjs::Str::make(m_domain, m_domain_read_ptr-1));
    } else {
      char addr[100];
      std::sprintf(
        addr, "%d.%d.%d.%d",
        m_command[4],
        m_command[5],
        m_command[6],
        m_command[7]
      );
      argv[0].set(addr);
    }
    int port = (int(m_command[2]) << 8) + m_command[3];
    argv[1].set(port);
    if (m_user_id_read_ptr > 0) {
      argv[2].set(pjs::Str::make(m_user_id, m_user_id_read_ptr-1));
    }
    callback(*ctx, m_on_connect, 3, argv, ret);
    if (ret.to_boolean()) {
      auto root = static_cast<Context*>(ctx->root());
      auto mod = pipeline()->module();
      if (auto pipeline = mod->find_named_pipeline(m_target)) {
        auto session = Session::make(root, pipeline);
        session->on_output(out());
        m_session = session;
        reply(0x5a);
        return;
      } else {
        Log::error("[link] unknown pipeline: %s", m_target->c_str());
      }
    }
    reply(0x5b);
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
          case READ_COMMAND:
            m_command[m_command_read_ptr++] = c;
            if (m_command_read_ptr >= sizeof(m_command)) {
              if (m_command[0] != 0x04 ||
                  m_command[1] != 0x01
              ) {
                reply(0x5b);
                close(SessionEnd::make());
              } else {
                m_state = READ_USER_ID;
              }
            }
            break;
          case READ_USER_ID:
            m_user_id[m_user_id_read_ptr++] = c;
            if (m_user_id_read_ptr >= sizeof(m_user_id) || !c) {
              if (!m_command[4] &&
                  !m_command[5] &&
                  !m_command[6]
              ) {
                m_state = READ_DOMAIN;
              } else {
                connect();
                return true;
              }
            }
            break;
          case READ_DOMAIN:
            m_domain[m_domain_read_ptr++] = c;
            if (m_domain_read_ptr >= sizeof(m_domain) || !c) {
              connect();
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
    close(inp);
  }
}

} // namespace pipy