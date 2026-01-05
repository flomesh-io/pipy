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
 *  SOFTWARE IS PROVIDED IN AN "AS IS" CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef API_NAT_HPP
#define API_NAT_HPP

#include "pjs/pjs.hpp"
#include "nat-traversal.hpp"

#include <memory>

namespace pipy {
namespace nat {

//
// NAT - JavaScript API for NAT traversal
//

class NAT : public pjs::ObjectTemplate<NAT> {
public:
  auto discover_public_address(pjs::Str *stun_server, int stun_port) -> pjs::Promise*;
  auto connect_p2p(
    pjs::Str *peer_public_ip, int peer_public_port,
    pjs::Str *peer_private_ip, int peer_private_port,
    int local_port
  ) -> pjs::Promise*;
  auto test_connectivity(pjs::Str *peer_ip, int peer_port) -> pjs::Promise*;

private:
  NAT() {}

  friend class pjs::ObjectTemplate<NAT>;
};

} // namespace nat
} // namespace pipy

#endif // API_NAT_HPP
