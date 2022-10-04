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

#ifndef HAPROXY_HPP
#define HAPROXY_HPP

#include "filter.hpp"

namespace pipy {

namespace haproxy {

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

  pjs::Ref<pjs::Function> m_on_connect;
  pjs::Ref<Pipeline> m_pipeline;
  int m_version = 0;
  int m_header_read_ptr = 0;
  int m_header_read_chr = 0;
  int m_address_size_v2 = 0;
  char m_header[232];
  bool m_error = false;

  void parse_header_v1();
  void parse_header_v2();
  void start(pjs::Value &obj);
  void error();
};

//
// Client
//

class Client : public Filter {
public:
  Client(const pjs::Value &target);

private:
  Client(const Client &r);
  ~Client();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Value m_target;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::PropertyCache m_prop_version;
  pjs::PropertyCache m_prop_command;
  pjs::PropertyCache m_prop_protocol;
  pjs::PropertyCache m_prop_source_address;
  pjs::PropertyCache m_prop_target_address;
  pjs::PropertyCache m_prop_source_port;
  pjs::PropertyCache m_prop_target_port;
  bool m_error = false;
};

} // namespace haproxy
} // namespace pipy

#endif // HAPROXY_HPP
