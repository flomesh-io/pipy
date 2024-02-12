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

#include "listen.hpp"
#include "listener.hpp"
#include "worker.hpp"
#include "api/pipeline.hpp"

namespace pipy {

thread_local static pjs::ConstStr s_tcp("tcp");
thread_local static pjs::ConstStr s_udp("udp");

void Listen::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  auto instance = ctx.root()->instance();
  auto worker = instance ? static_cast<Worker*>(instance) : nullptr;

  int i = 0;
  int port = 0;
  pjs::Str *address = nullptr;
  pjs::Str *protocol = nullptr;
  pjs::Object *options = nullptr;
  pjs::Function *builder = nullptr;

  if (ctx.get(i, address) || ctx.get(i, port)) i++;
  else {
    ctx.error_argument_type(i, "a number or a string");
    return;
  }

  if (ctx.get(i, protocol)) i++;

  if (!ctx.get(i, builder)) {
    if (!ctx.check(i, options)) return;
    if (!ctx.check(i + 1, builder)) return;
  }

  Listener::Protocol proto = Listener::Protocol::TCP;
  if (protocol) {
    if (protocol == s_tcp) proto = Listener::Protocol::TCP; else
    if (protocol == s_udp) proto = Listener::Protocol::UDP; else {
      ctx.error("unknown protocol");
      return;
    }
  }

  std::string ip;

  if (address) {
    if (!utils::get_host_port(address->str(), ip, port)) {
      ctx.error("invalid 'address:port' form");
      return;
    }
    uint8_t buf[16];
    if (!utils::get_ip_v4(ip, buf) && !utils::get_ip_v6(ip, buf)) {
      ctx.error("invalid IP address");
      return;
    }
  } else {
    ip = "0.0.0.0";
  }

  if (port < 1 || 65535 < port) {
    ctx.error("port out of range");
    return;
  }

  PipelineLayout *pl = nullptr;
  if (builder) {
    pl = PipelineDesigner::make_pipeline_layout(ctx, builder);
    if (!pl) return;
  }

  auto l = Listener::get(proto, ip, port);
  if (!l->set_next_state(pl, options) && worker && !worker->started() && !worker->forced()) {
    l->rollback();
    ctx.error("unable to listen on [" + ip + "]:" + std::to_string(port));
    return;
  }

  if (!worker || worker->started()) {
    l->commit();
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Listen>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
