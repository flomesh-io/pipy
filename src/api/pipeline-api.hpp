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

#ifndef API_PIPELINE_HPP
#define API_PIPELINE_HPP

#include "net.hpp"
#include "pipeline.hpp"
#include "filter.hpp"

#include <list>
#include <set>

namespace pipy {

//
// PipelineDesigner
//

class PipelineDesigner : public pjs::ObjectTemplate<PipelineDesigner> {
public:
  static auto make_pipeline_layout(
    pjs::Context &ctx,
    pjs::Function *builder
  ) -> PipelineLayout*;

  auto trace_location(pjs::Context &ctx) -> PipelineDesigner*;

  void on_start(pjs::Object *starting_events);
  void on_end(pjs::Function *handler);
  void to(PipelineLayout *layout);
  void close();

  void accept_http_tunnel(pjs::Function *handler);
  void accept_proxy_protocol(pjs::Function *handler);
  void accept_socks(pjs::Function *handler);
  void accept_tls(pjs::Object *options);
  void compress(const pjs::Value &algorithm);
  void compress_http(const pjs::Value &algorithm);
  void connect(const pjs::Value &target, pjs::Object *options);
  void connect_http_tunnel(pjs::Object *handshake, pjs::Object *options);
  void connect_proxy_protocol(const pjs::Value &address);
  void connect_socks(const pjs::Value &address);
  void connect_tls(pjs::Object *options);
  void decode_bgp(pjs::Object *options);
  void decode_dubbo();
  void decode_http_request();
  void decode_http_response(pjs::Object *options);
  void decode_mqtt();
  void decode_multipart();
  void decode_netlink();
  void decode_resp();
  void decode_thrift();
  void decode_websocket();
  void decompress(const pjs::Value &algorithm);
  void decompress_http();
  void deframe(pjs::Object *states);
  void demux();
  void demux_fcgi();
  void demux_http(pjs::Object *options);
  void demux_queue();
  void detect_protocol(pjs::Function *handler, pjs::Object *options);
  void dummy();
  void dump(const pjs::Value &tag);
  void encode_bgp(pjs::Object *options);
  void encode_dubbo();
  void encode_http_request(pjs::Object *options);
  void encode_http_response(pjs::Object *options);
  void encode_mqtt();
  void encode_netlink();
  void encode_resp();
  void encode_thrift();
  void encode_websocket();
  void exec(const pjs::Value &command, pjs::Object *options);
  void fork(const pjs::Value &init_args);
  void fork_join(const pjs::Value &init_args);
  void fork_race(const pjs::Value &init_args);
  void handle(Event::Type type, pjs::Function *handler);
  void handle_body(pjs::Function *handler, pjs::Object *options);
  void handle_message(pjs::Function *handler, pjs::Object *options);
  void handle_message_one(pjs::Function *handler, pjs::Object *options);
  void handle_start(pjs::Function *handler);
  void handle_tls_client_hello(pjs::Function *handler);
  void insert(pjs::Object *events);
  void loop();
  void mux(pjs::Function *session_selector, pjs::Object *options);
  void mux_fcgi(pjs::Function *session_selector, pjs::Object *options);
  void mux_http(pjs::Function *session_selector, pjs::Object *options);
  void mux_queue(pjs::Function *session_selector, pjs::Object *options);
  void pipe(const pjs::Value &target, pjs::Object *target_map, pjs::Object *init_args);
  void pipe_next(const pjs::Value &args);
  void print();
  void read(const pjs::Value &filename, pjs::Object *options);
  void repeat(pjs::Function *condition);
  void replace(Event::Type type, pjs::Object *replacement);
  void replace_body(pjs::Object *replacement, pjs::Object *options);
  void replace_message(pjs::Object *replacement, pjs::Object *options);
  void replace_message_one(pjs::Object *replacement, pjs::Object *options);
  void replace_start(pjs::Object *replacement);
  void serve_http(pjs::Object *handler, pjs::Object *options);
  void split(const pjs::Value &separator);
  void swap(const pjs::Value &hub);
  void tee(const pjs::Value &filename, pjs::Object *options);
  void throttle_concurrency(pjs::Object *quota, pjs::Object *options);
  void throttle_data_rate(pjs::Object *quota, pjs::Object *options);
  void throttle_message_rate(pjs::Object *quota, pjs::Object *options);
  void wait(pjs::Function *condition, pjs::Object *options);

private:
  PipelineDesigner(PipelineLayout *layout)
    : m_layout(layout) {}

  void check_integrity();
  auto append_filter(Filter *filter) -> Filter*;
  void require_sub_pipeline(Filter *filter);

  PipelineLayout* m_layout;
  Filter* m_current_filter = nullptr;
  Filter* m_current_joint_filter = nullptr;
  pjs::Location m_current_location;
  bool m_has_on_start = false;
  bool m_has_on_end = false;

  friend class pjs::ObjectTemplate<PipelineDesigner>;
};

//
// PipelineLayoutWrapper
//

class PipelineLayoutWrapper : public pjs::ObjectTemplate<PipelineLayoutWrapper> {
public:

  //
  // PipelineLayoutWrapper::Constructor
  //

  class Constructor : public pjs::FunctionTemplate<Constructor> {
  public:
    void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret);
  };

  auto get() const -> PipelineLayout* { return m_layout; }
  auto instantiate(Context *ctx) -> Pipeline*;

private:
  PipelineLayoutWrapper(PipelineLayout *layout)
    : m_layout(layout) {}

  pjs::Ref<PipelineLayout> m_layout;

  friend class pjs::ObjectTemplate<PipelineLayoutWrapper>;
};

//
// PipelineWrapper
//

class PipelineWrapper :
  public pjs::RefCount<PipelineWrapper>,
  public pjs::Pooled<PipelineWrapper>,
  public EventTarget,
  public Pipeline::ResultCallback
{
public:
  PipelineWrapper(Pipeline *pipeline)
    : m_pipeline(pipeline) {}

  auto spawn(int argc, pjs::Value argv[]) -> pjs::Promise*;
  auto process(pjs::Object *events) -> pjs::Promise*;

private:
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<pjs::Promise::Settler> m_settler;
  pjs::Ref<pjs::Function> m_generator;
  pjs::Ref<pjs::Promise::Callback> m_events_callback;

  void generate();
  bool feed(const pjs::Value &events);
  void close();

  virtual void on_event(Event *evt) override;
  virtual void on_pipeline_result(Pipeline *p, pjs::Value &result) override;

  friend class pjs::ObjectTemplate<PipelineWrapper>;
};

//
// Hub
//

class Hub : public pjs::ObjectTemplate<Hub> {
public:
  void join(EventTarget::Input *party);
  void exit(EventTarget::Input *party);
  void broadcast(Event *evt, EventTarget::Input *from = nullptr);

private:
  Hub() {}
  ~Hub() {}

  struct PartyChange { bool join; pjs::Ref<EventTarget::Input> party; };

  pjs::Ref<EventTarget::Input> m_pair[2];
  std::set<pjs::Ref<EventTarget::Input>> m_parties;
  std::list<PartyChange> m_changing_parties;
  bool m_broadcasting = false;

  friend class pjs::ObjectTemplate<Hub>;
};

} // namespace pipy

#endif // API_PIPELINE_HPP
