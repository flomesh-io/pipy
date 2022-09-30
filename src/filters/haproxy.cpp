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

#include "haproxy.hpp"
#include "data.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "log.hpp"

namespace pipy {
namespace haproxy {

static pjs::ConstStr s_TCP4("TCP4");
static pjs::ConstStr s_TCP6("TCP6");
static pjs::ConstStr s_UNKNOWN("UNKNOWN");
static pjs::ConstStr s_protocol("protocol");
static pjs::ConstStr s_sourceAddress("sourceAddress");
static pjs::ConstStr s_sourcePort("sourcePort");
static pjs::ConstStr s_targetAddress("targetAddress");
static pjs::ConstStr s_targetPort("targetPort");

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
  d.name = "acceptHAProxy";
}

auto Server::clone() -> Filter* {
  return new Server(*this);
}

void Server::reset() {
  Filter::reset();
  m_pipeline = nullptr;
  m_header_read_ptr = 0;
  m_header_read_chr = 0;
  m_error = false;
}

void Server::process(Event *evt) {
  if (auto data = evt->as<Data>()) {
    if (!m_pipeline && !m_error) {
      Data out;
      data->shift_to(
        [this](int c) {
          if (c == '\n' && m_header_read_chr == '\r') {
            m_header[m_header_read_ptr - 1] = ' ';
            m_header[m_header_read_ptr] = '\0';
            parse_header();
            return true;
          }
          if (m_header_read_ptr >= sizeof(m_header)) {
            parse_error();
            return true;
          }
          m_header_read_chr = c;
          m_header[m_header_read_ptr++] = c;
          return false;
        },
        out
      );
    }
    if (m_pipeline) {
      Filter::output(data, m_pipeline->input());
    }
  }
}

void Server::parse_header() {
  auto *p = m_header;
  if (std::memcmp(p, "PROXY ", 6)) {
    parse_error();
    return;
  } else {
    p += 6;
  }

  pjs::Ref<pjs::Str> protocol;
  if (!std::memcmp(p, "TCP4 ", 5)) { protocol = s_TCP4; p += 5; } else
  if (!std::memcmp(p, "TCP6 ", 5)) { protocol = s_TCP6; p += 5; } else
  if (!std::memcmp(p, "UNKNOWN ", 8)) { protocol = s_UNKNOWN; p += 8; }
  else { parse_error(); return; }

  auto next_field = [](char **ptr) -> char* {
    auto n = 0;
    auto p = *ptr;
    while (p[n] && p[n] != ' ') n++;
    if (!n) return nullptr;
    p[n] = '\0';
    *ptr = p + n + 1;
    return p;
  };

  pjs::Value obj(pjs::Object::make());
  obj.o()->set(s_protocol, protocol.get());

  if (protocol != s_UNKNOWN) {
    uint8_t ip[16];
    const char *s[4];

    for (int i = 0; i < 4; i++) {
      if (!(s[i] = next_field(&p))) {
        parse_error();
        return;
      }
    }

    if (protocol == s_TCP6) {
      if (!utils::get_ip_v6(s[0], ip) || !utils::get_ip_v6(s[1], ip)) {
        parse_error();
        return;
      }
    } else {
      if (!utils::get_ip_v4(s[0], ip) || !utils::get_ip_v4(s[1], ip)) {
        parse_error();
        return;
      }
    }

    obj.o()->set(s_sourceAddress, pjs::Str::make(s[0], s[1] - s[0] - 1));
    obj.o()->set(s_targetAddress, pjs::Str::make(s[1], s[2] - s[1] - 1));
    obj.o()->set(s_sourcePort, std::atoi(s[2]));
    obj.o()->set(s_targetPort, std::atoi(s[3]));
  }

  pjs::Value ret;
  if (!Filter::callback(m_on_connect, 1, &obj, ret) || !ret.to_boolean()) {
    parse_error();
    return;
  }

  m_pipeline = Filter::sub_pipeline(0, false, Filter::output());
}

void Server::parse_error() {
  Filter::output(StreamEnd::make());
  m_error = true;
}

//
// Client
//

static Data::Producer s_dp("connectHAProxy");

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
  d.name = "connectHAProxy";
}

auto Client::clone() -> Filter* {
  return new Client(*this);
}

void Client::reset() {
  Filter::reset();
  m_pipeline = nullptr;
}

void Client::process(Event *evt) {
}

} // namespace haproxy
} // namespace pipy
