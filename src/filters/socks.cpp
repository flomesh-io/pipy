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

static Data::Producer s_dp("SOCKS");

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
  EventSource::close();
  Deframer::reset(STATE_INIT);
  Deframer::pass_all(false);
  m_buffer.clear();
  m_pipeline = nullptr;
  m_eos = nullptr;
  m_is_started = false;
}

void Client::process(Event *evt) {
  if (!m_pipeline) {
    m_pipeline = sub_pipeline(0, false, EventSource::reply())->start();
    uint8_t greeting[] = { 0x05, 1, 0 };
    Filter::output(s_dp.make(greeting, sizeof(greeting)), m_pipeline->input());
  }

  if (m_is_started) {
    if (!m_buffer.empty()) {
      Filter::output(Data::make(std::move(m_buffer)), m_pipeline->input());
    }
    Filter::output(evt, m_pipeline->input());
  } else if (auto data = evt->as<Data>()) {
    m_buffer.push(*data);
  } else if (auto eos = evt->as<StreamEnd>()) {
    m_eos = eos;
  }
}

void Client::on_reply(Event *evt) {
  if (m_is_started || evt->is<StreamEnd>()) {
    Filter::output(evt);
  } else if (auto data = evt->as<Data>()) {
    Deframer::deframe(*data);
  }
}

auto Client::on_state(int state, int c) -> int {
  switch (state) {
    case STATE_INIT:
      m_read_buffer[0] = c;
      return STATE_READ_AUTH;
    case STATE_READ_AUTH:
      m_read_buffer[1] = c;
      if (
        m_read_buffer[0] == 0x05 &&
        m_read_buffer[1] == 0x00
      ) {
        if (start()) {
          Deframer::read(3, m_read_buffer);
          return STATE_READ_CONN_HEAD;
        }
      }
      break;
    case STATE_READ_CONN_HEAD:
      if (
        m_read_buffer[0] == 0x05 &&
        m_read_buffer[1] == 0x00 &&
        m_read_buffer[2] == 0x00
      ) {
        Deframer::read(2, m_read_buffer);
        return STATE_READ_CONN_ADDR;
      }
      break;
    case STATE_READ_CONN_ADDR:
      switch (m_read_buffer[0]) {
        case 0x01: Deframer::read(4-1+2, m_read_buffer); return STATE_CONNECTED;
        case 0x04: Deframer::read(16-1+2, m_read_buffer); return STATE_CONNECTED;
        case 0x03: Deframer::read(m_read_buffer[1]+2, m_read_buffer); return STATE_CONNECTED;
      }
      break;
    case STATE_CONNECTED:
      Deframer::pass_all(true);
      m_is_started = true;
      if (m_eos) {
        EventFunction::input()->input_async(m_eos);
      } else {
        EventFunction::input()->flush_async();
      }
      return STATE_CONNECTED;
  }
  Filter::output(StreamEnd::make());
  return -1;
}

void Client::on_pass(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

bool Client::start() {
  pjs::Value target;
  if (!eval(m_target, target)) return false;
  if (!target.is_string()) {
    Filter::error("target is not or did not return a string");
    return false;
  }
  auto s = target.s();
  std::string host; int port;
  if (!utils::get_host_port(s->str(), host, port)) {
    Filter::error("invalid target: %s", s->c_str());
    return false;
  }

  size_t len = 0;
  uint8_t buf[300];
  buf[0] = 0x05;
  buf[1] = 0x01;
  buf[2] = 0x00;

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
      return false;
    }
    auto n = host.length();
    buf[3] = 0x03;
    buf[4] = n;
    std::memcpy(buf + 5, host.c_str(), n);
    buf[5 + n] = (port >> 8) & 0xff;
    buf[6 + n] = (port >> 0) & 0xff;
    len = 5 + n + 2;
  }

  Filter::output(s_dp.make(buf, len), m_pipeline->input());
  return true;
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
