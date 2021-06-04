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

#ifndef SOCKS4_HPP
#define SOCKS4_HPP

#include "filter.hpp"

namespace pipy {

class Session;

//
// ProxySOCKS4
//

class ProxySOCKS4 : public Filter {
public:
  ProxySOCKS4();
  ProxySOCKS4(pjs::Str *target, pjs::Function *on_connect);

private:
  ProxySOCKS4(const ProxySOCKS4 &r);
  ~ProxySOCKS4();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  enum State {
    READ_COMMAND,
    READ_USER_ID,
    READ_DOMAIN,
  };

  pjs::Ref<pjs::Str> m_target;
  pjs::Ref<pjs::Function> m_on_connect;
  pjs::Ref<Session> m_session;
  State m_state = READ_COMMAND;
  uint8_t m_command[8];
  char m_user_id[256];
  char m_domain[256];
  int m_command_read_ptr = 0;
  int m_user_id_read_ptr = 0;
  int m_domain_read_ptr = 0;
  bool m_session_end = false;
};

} // namespace pipy

#endif // SOCKS4_HPP