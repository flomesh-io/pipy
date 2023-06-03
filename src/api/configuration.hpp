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

#ifndef API_CONFIGURATION_HPP
#define API_CONFIGURATION_HPP

#include "net.hpp"
#include "event.hpp"
#include "listener.hpp"
#include "nmi.hpp"

#include <list>
#include <map>
#include <memory>

namespace pipy {

class Configuration;
class Filter;
class Graph;
class Worker;
class Module;
class JSModule;

//
// FilterConfigurator
//

class FilterConfigurator : public pjs::ObjectTemplate<FilterConfigurator> {
public:
  void on_start(pjs::Object *starting_events);
  void on_start(pjs::Function *handler);
  void on_end(pjs::Function *handler);

  void accept_http_tunnel(pjs::Function *handler);
  void accept_proxy_protocol(pjs::Function *handler);
  void accept_socks(pjs::Function *on_connect);
  void accept_tls(pjs::Object *options);
  void branch(int count, pjs::Function **conds, const pjs::Value *layouts);
  void branch_message_start(int count, pjs::Function **conds, const pjs::Value *layouts);
  void branch_message(int count, pjs::Function **conds, const pjs::Value *layouts);
  void chain(const std::list<JSModule*> modules);
  void chain_next();
  void compress(const pjs::Value &algorithm);
  void compress_http(const pjs::Value &algorithm);
  void connect(const pjs::Value &target, pjs::Object *options);
  void connect_http_tunnel(pjs::Object *handshake);
  void connect_proxy_protocol(const pjs::Value &address);
  void connect_socks(const pjs::Value &address);
  void connect_tls(pjs::Object *options);
  void decode_bgp(pjs::Object *options);
  void decode_dubbo();
  void decode_http_request(pjs::Function *handler);
  void decode_http_response(pjs::Function *handler);
  void decode_mqtt();
  void decode_multipart();
  void decode_resp();
  void decode_thrift();
  void decode_websocket();
  void decompress(const pjs::Value &algorithm);
  void decompress_http();
  void deframe(pjs::Object *states);
  void demux(pjs::Object *options);
  void demux_http(pjs::Object *options);
  void deposit_message(const pjs::Value &filename, pjs::Object *options);
  void detect_protocol(pjs::Function *callback);
  void dummy();
  void dump(const pjs::Value &tag);
  void encode_bgp(pjs::Object *options);
  void encode_dubbo();
  void encode_http_request(pjs::Object *options, pjs::Function *handler);
  void encode_http_response(pjs::Object *options, pjs::Function *handler);
  void encode_mqtt();
  void encode_resp();
  void encode_thrift();
  void encode_websocket();
  void exec(const pjs::Value &command);
  void fork(const pjs::Value &init_arg);
  void handle_body(pjs::Function *callback, pjs::Object *options);
  void handle_event(Event::Type type, pjs::Function *callback);
  void handle_message(pjs::Function *callback, pjs::Object *options);
  void handle_start(pjs::Function *callback);
  void handle_tls_client_hello(pjs::Function *callback);
  void link(pjs::Function *name = nullptr);
  void loop();
  void mux(pjs::Function *session_selector, pjs::Object *options);
  void mux_http(pjs::Function *session_selector, pjs::Object *options);
  void pack(int batch_size, pjs::Object *options);
  void print();
  void read(const pjs::Value &pathname);
  void replace_body(pjs::Object *replacement, pjs::Object *options);
  void replace_event(Event::Type type, pjs::Object *replacement);
  void replace_message(pjs::Object *replacement, pjs::Object *options);
  void replace_start(pjs::Object *replacement);
  void replay(pjs::Object *options);
  void serve_http(pjs::Object *handler, pjs::Object *options);
  void split(Data *separator);
  void split(pjs::Str *separator);
  void split(pjs::Function *callback);
  void tee(const pjs::Value &filename, pjs::Object *options);
  void throttle_concurrency(pjs::Object *quota, pjs::Object *options);
  void throttle_data_rate(pjs::Object *quota, pjs::Object *options);
  void throttle_message_rate(pjs::Object *quota, pjs::Object *options);
  void use(JSModule *module, pjs::Str *pipeline);
  void use(nmi::NativeModule *module, pjs::Str *pipeline);
  void use(const std::list<JSModule*> modules, pjs::Str *pipeline, pjs::Function *when);
  void use(const std::list<JSModule*> modules, pjs::Str *pipeline, pjs::Str *pipeline_down, pjs::Function *when);
  void wait(pjs::Function *condition, pjs::Object *options);

  auto trace_location(pjs::Context &ctx) -> FilterConfigurator*;
  void to(pjs::Str *layout_name);
  void to(const std::string &name, const std::function<void(FilterConfigurator*)> &cb);
  auto sub_pipeline(const std::string &name, const std::function<void(FilterConfigurator*)> &cb) -> int;
  bool get_branches(pjs::Context &ctx, int n, pjs::Function **conds, pjs::Value *layouts);
  void check_integrity();

protected:
  struct PipelineConfig {
    int index;
    pjs::Context::Location on_start_location;
    pjs::Ref<pjs::Object> on_start;
    pjs::Ref<pjs::Function> on_end;
    std::list<std::unique_ptr<Filter>> filters;
  };

  FilterConfigurator(
    Configuration *configuration,
    PipelineConfig *config = nullptr
  ) : m_configuration(configuration)
    , m_config(config) {}

  void set_pipeline_config(PipelineConfig *config) {
    m_config = config;
    m_current_filter = nullptr;
    m_current_joint_filter = nullptr;
  }

private:
  auto append_filter(Filter *filter) -> Filter*;
  void require_sub_pipeline(Filter *filter);

  Configuration* m_configuration;
  PipelineConfig* m_config;
  Filter* m_current_filter = nullptr;
  Filter* m_current_joint_filter = nullptr;
  pjs::Context::Location m_current_location;

  friend class pjs::ObjectTemplate<FilterConfigurator>;
};

//
// Configuration
//

class Configuration : public pjs::ObjectTemplate<Configuration, FilterConfigurator> {
public:
  struct Export {
    pjs::Ref<pjs::Str> ns;
    pjs::Ref<pjs::Str> name;
    pjs::Value value;
  };

  struct Import {
    pjs::Ref<pjs::Str> ns;
    pjs::Ref<pjs::Str> name;
    pjs::Ref<pjs::Str> original_name;
  };

  void add_export(pjs::Str *ns, pjs::Object *variables);
  void add_import(pjs::Object *variables);

  void listen(int port, pjs::Object *options);
  void listen(const std::string &port, pjs::Object *options);
  void listen(ListenerArray *listeners, pjs::Object *options);
  void task(const std::string &when);
  void watch(const std::string &filename);
  void pipeline(const std::string &name);
  void pipeline();

  void bind_pipelines();
  void bind_exports(Worker *worker, Module *module);
  void bind_imports(Worker *worker, Module *module, pjs::Expr::Imports *imports);
  void apply(JSModule *module);
  void draw(Graph &g);

private:
  Configuration(pjs::Object *context_prototype);

  struct ListenConfig : public PipelineConfig {
    pjs::Ref<ListenerArray> listeners;
    std::string ip;
    int port;
  };

  struct TaskConfig : public PipelineConfig {
    std::string name;
    std::string when;
  };

  struct WatchConfig : public PipelineConfig {
    std::string filename;
  };

  struct NamedPipelineConfig : public PipelineConfig {
    std::string name;
  };

  pjs::Ref<pjs::Object> m_context_prototype;
  pjs::Ref<pjs::Class> m_context_class;
  std::list<Export> m_exports;
  std::list<Import> m_imports;
  std::list<ListenConfig> m_listens;
  std::list<TaskConfig> m_tasks;
  std::list<WatchConfig> m_watches;
  std::list<NamedPipelineConfig> m_named_pipelines;
  std::map<int, NamedPipelineConfig> m_indexed_pipelines;
  std::unique_ptr<PipelineConfig> m_entrance_pipeline;
  int m_current_pipeline_index = 0;

  auto next_pipeline_index() -> int { return m_current_pipeline_index++; }
  auto new_indexed_pipeline(const std::string &name, int &index) -> FilterConfigurator*;

  friend class pjs::ObjectTemplate<Configuration, FilterConfigurator>;
  friend class FilterConfigurator;
};

} // namespace pipy

#endif // API_CONFIGURATION_HPP
