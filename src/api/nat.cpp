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

#include "nat.hpp"
#include "net.hpp"
#include "log.hpp"

namespace pipy {
namespace nat {

//
// NAT
//

auto NAT::discover_public_address(pjs::Str *stun_server, int stun_port) -> pjs::Promise* {
  auto promise = pjs::Promise::make();
  auto settler = pjs::Promise::Settler::make(promise);

  auto client = std::make_shared<STUNClient>(Net::context());

  client->discover(
    stun_server->str(),
    stun_port,
    [settler, client](const std::error_code &ec, const STUNClient::PublicEndpoint &endpoint) {
      if (ec) {
        auto err_msg = pjs::Str::make(ec.message());
        settler->reject(err_msg);
      } else {
        auto result = pjs::Object::make();
        result->set("ip", pjs::Str::make(endpoint.ip));
        result->set("port", endpoint.port);
        result->set("isV6", endpoint.is_v6);
        settler->resolve(result);
      }
    }
  );

  return promise;
}

auto NAT::connect_p2p(
  pjs::Str *peer_public_ip, int peer_public_port,
  pjs::Str *peer_private_ip, int peer_private_port,
  int local_port
) -> pjs::Promise* {
  auto promise = pjs::Promise::make();
  auto settler = pjs::Promise::Settler::make(promise);

  auto puncher = std::make_shared<HolePuncher>(Net::context());

  HolePuncher::PeerInfo peer;
  peer.public_ip = peer_public_ip->str();
  peer.public_port = peer_public_port;
  peer.private_ip = peer_private_ip ? peer_private_ip->str() : "";
  peer.private_port = peer_private_port;

  puncher->connect(
    peer,
    local_port,
    [settler, puncher](const HolePuncher::ConnectionResult &result) {
      if (result.success) {
        auto conn = pjs::Object::make();
        conn->set("ip", pjs::Str::make(result.endpoint.address().to_string()));
        conn->set("port", result.endpoint.port());
        settler->resolve(conn);
      } else {
        auto err_msg = pjs::Str::make(result.error_message);
        settler->reject(err_msg);
      }
    }
  );

  return promise;
}

auto NAT::test_connectivity(pjs::Str *peer_ip, int peer_port) -> pjs::Promise* {
  auto promise = pjs::Promise::make();
  auto settler = pjs::Promise::Settler::make(promise);

  auto tester = std::make_shared<ConnectivityTester>(Net::context());

  tester->test(
    peer_ip->str(),
    peer_port,
    [settler, tester](const std::error_code &ec, const ConnectivityTester::TestResult &result) {
      if (ec) {
        auto err_msg = pjs::Str::make(ec.message());
        settler->reject(err_msg);
      } else {
        auto res = pjs::Object::make();
        res->set("reachable", result.reachable);
        res->set("latency", result.latency_ms);
        res->set("packetLoss", result.packet_loss);
        settler->resolve(res);
      }
    }
  );

  return promise;
}

} // namespace nat
} // namespace pipy

namespace pjs {

using namespace pipy::nat;

//
// NAT
//

template<> void ClassDef<NAT>::init() {
  ctor();

  method("discoverPublicAddress", [](Context &ctx, Object *obj, Value &ret) {
    Str *stun_server;
    int stun_port = 3478;
    if (!ctx.arguments(1, &stun_server, &stun_port)) return;
    ret.set(obj->as<NAT>()->discover_public_address(stun_server, stun_port));
  });

  method("connectP2P", [](Context &ctx, Object *obj, Value &ret) {
    Str *peer_public_ip, *peer_private_ip = nullptr;
    int peer_public_port, peer_private_port = 0, local_port = 0;
    if (!ctx.arguments(2, &peer_public_ip, &peer_public_port, &peer_private_ip, &peer_private_port, &local_port)) return;
    ret.set(obj->as<NAT>()->connect_p2p(
      peer_public_ip, peer_public_port,
      peer_private_ip, peer_private_port,
      local_port
    ));
  });

  method("testConnectivity", [](Context &ctx, Object *obj, Value &ret) {
    Str *peer_ip;
    int peer_port;
    if (!ctx.arguments(2, &peer_ip, &peer_port)) return;
    ret.set(obj->as<NAT>()->test_connectivity(peer_ip, peer_port));
  });
}

template<> void ClassDef<Constructor<NAT>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
