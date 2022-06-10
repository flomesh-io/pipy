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

#ifndef SOCKS_HPP
#define SOCKS_HPP

#include "filter.hpp"
#include "data.hpp"

namespace pipy {

namespace socks {

//
// Server
//

class Server : public Filter {
public:
  Server(pjs::Function *on_connect);

private:
  Server(const Server &r);
  ~Server();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  enum State {
    READ_VERSION,

    // SOCKS4
    READ_SOCKS4_CMD,
    READ_SOCKS4_DSTPORT,
    READ_SOCKS4_DSTIP,
    READ_SOCKS4_ID,
    READ_SOCKS4_DOMAIN,

    // SOCKS5
    READ_SOCKS5_NAUTH,
    READ_SOCKS5_AUTH,
    READ_SOCKS5_CMD,
    READ_SOCKS5_ADDR_TYPE,
    READ_SOCKS5_DOMAIN_LEN,
    READ_SOCKS5_DOMAIN,
    READ_SOCKS5_DSTIP,
    READ_SOCKS5_DSTPORT,
  };

  pjs::Ref<pjs::Str> m_target;
  pjs::Ref<pjs::Function> m_on_connect;
  pjs::Ref<Pipeline> m_pipeline;
  State m_state = READ_VERSION;
  uint8_t m_port[2];
  uint8_t m_ip[4];
  uint8_t m_id[256];
  uint8_t m_domain[256];
  int m_read_len = 0;
  int m_read_ptr = 0;
};

//
// Client
//

class ClientReceiver : public EventTarget {
  virtual void on_event(Event *evt) override;
};

class Client : public Filter, public ClientReceiver {
public:
  Client(const pjs::Value &target);

private:
  Client(const Client &r);
  ~Client();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  void on_receive(Event *evt);
  void send(Data *data);
  void send(const uint8_t *buf, size_t len);
  void connect();
  void close(StreamEnd::Error err);

  enum State {
    STATE_INIT,
    STATE_READ_AUTH,
    STATE_READ_CONN_HEAD,
    STATE_READ_CONN_ADDR,
    STATE_READ_CONN_ADDR_IPV4,
    STATE_READ_CONN_ADDR_IPV6,
    STATE_READ_CONN_ADDR_DOMAIN,
    STATE_CONNECTED,
    STATE_CLOSED,
  };

  pjs::Value m_target;
  pjs::Ref<Pipeline> m_pipeline;
  State m_state = STATE_INIT;
  Data m_buffer;
  uint8_t m_read_buffer[3];
  int m_read_size = 0;

  friend class ClientReceiver;
};

} // namespace socks
} // namespace pipy

#endif // SOCKS_HPP
