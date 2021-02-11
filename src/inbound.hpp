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

#ifndef INBOUND_HPP
#define INBOUND_HPP

#include "ns.hpp"
#include "net.hpp"
#include "pool.hpp"

#include <functional>
#include <list>
#include <memory>

NS_BEGIN

class Context;
class Data;
class Pipeline;
class Session;

//
// Inbound
//

class Inbound : public Pooled<Inbound> {
public:
  Inbound();
  ~Inbound();

  void accept(
    Pipeline* pipeline,
    asio::ip::tcp::acceptor &acceptor,
    std::function<void(const std::error_code&)> on_result
  );

  void send(std::unique_ptr<Data> data);
  void flush();
  void end();

private:
  Session* m_session = nullptr;
  asio::ip::tcp::socket m_socket;
  asio::ip::tcp::endpoint m_peer;
  std::string m_peer_addr;
  Data m_buffer;
  bool m_pumping = false;
  bool m_reading_ended = false;
  bool m_writing_ended = false;

  void receive();
  void pump();
  void close();
  void free();
};

NS_END

#endif // INBOUND_HPP