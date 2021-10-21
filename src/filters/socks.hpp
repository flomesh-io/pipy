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

namespace pipy {

class Session;

namespace socks {

//
// Server
//

class Server : public Filter {
public:
  Server();
  Server(pjs::Str *target, pjs::Function *on_connect);

private:
  Server(const Server &r);
  ~Server();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

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

  Pipeline* m_pipeline = nullptr;
  pjs::Ref<pjs::Str> m_target;
  pjs::Ref<pjs::Function> m_on_connect;
  pjs::Ref<Session> m_session;
  State m_state = READ_VERSION;
  uint8_t m_port[2];
  uint8_t m_ip[4];
  uint8_t m_id[256];
  uint8_t m_domain[256];
  int m_read_len = 0;
  int m_read_ptr = 0;
  bool m_session_end = false;
};

} // namespace socks
} // namespace pipy

#endif // SOCKS_HPP