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
  void on_start(pjs::Function *handler);
  void on_end(pjs::Function *handler);

  void accept_http_tunnel(pjs::Function *handler);
  void accept_proxy_protocol(pjs::Function *handler);
  void accept_socks(pjs::Function *on_connect);
  void accept_tls(pjs::Object *options);
  void branch(int count, pjs::Function **conds, const pjs::Value *layout);
  void chain(const std::list<JSModule*> modules);
  void chain_next();
  void compress_http(pjs::Object *options);
  void compress_message(pjs::Object *options);
  void connect(const pjs::Value &target, pjs::Object *options);
  void connect_http_tunnel(const pjs::Value &address);
  void connect_proxy_protocol(const pjs::Value &address);
  void connect_socks(const pjs::Value &address);
  void connect_tls(pjs::Object *options);
  void decode_dubbo();
  void decode_http_request();
  void decode_http_response(pjs::Object *options);
  void decode_mqtt(pjs::Object *options);
  void decode_multipart();
  void decode_thrift(pjs::Object *options);
  void decode_websocket();
  void decompress_http(pjs::Function *enable);
  void decompress_message(const pjs::Value &algorithm);
  void deframe(pjs::Object *states);
  void demux();
  void demux_queue(pjs::Object *options);
  void demux_http(pjs::Object *options);
  void deposit_message(const pjs::Value &filename, pjs::Object *options);
  void detect_protocol(pjs::Function *callback);
  void dummy();
  void dump(const pjs::Value &tag);
  void encode_dubbo(pjs::Object *message_obj);
  void encode_http_request(pjs::Object *options);
  void encode_http_response(pjs::Object *options);
  void encode_mqtt();
  void encode_thrift();
  void encode_websocket();
  void exec(const pjs::Value &command);
  void fork(const pjs::Value &init_arg);
  void handle_body(pjs::Function *callback, int size_limit);
  void handle_event(Event::Type type, pjs::Function *callback);
  void handle_message(pjs::Function *callback, int size_limit);
  void handle_start(pjs::Function *callback);
  void handle_tls_client_hello(pjs::Function *callback);
  void input(pjs::Function *callback);
  void link(size_t count, pjs::Str **targets, pjs::Function **conditions);
  void merge(pjs::Function *group, pjs::Object *options);
  void mux(pjs::Function *group, pjs::Object *options);
  void mux_queue(pjs::Function *group, pjs::Object *options);
  void mux_http(pjs::Function *group, pjs::Object *options);
  void output(pjs::Function *output_f);
  void pack(int batch_size, pjs::Object *options);
  void print();
  void replace_body(const pjs::Value &replacement, int size_limit);
  void replace_event(Event::Type type, const pjs::Value &replacement);
  void replace_message(const pjs::Value &replacement, int size_limit);
  void replace_start(const pjs::Value &replacement);
  void replay(pjs::Object *options);
  void serve_http(pjs::Object *handler);
  void split(Data *separator);
  void split(pjs::Str *separator);
  void split(pjs::Function *callback);
  void tee(const pjs::Value &filename);
  void throttle_concurrency(pjs::Object *quota);
  void throttle_data_rate(pjs::Object *quota);
  void throttle_message_rate(pjs::Object *quota);
  void use(JSModule *module, pjs::Str *pipeline);
  void use(nmi::NativeModule *module, pjs::Str *pipeline);
  void use(const std::list<JSModule*> modules, pjs::Str *pipeline, pjs::Function *when);
  void use(const std::list<JSModule*> modules, pjs::Str *pipeline, pjs::Str *pipeline_down, pjs::Function *when);
  void wait(pjs::Function *condition, pjs::Object *options);

  void to(pjs::Str *layout_name);
  void to(const std::string &name, const std::function<void(FilterConfigurator*)> &cb);
  auto sub_pipeline(const std::string &name, const std::function<void(FilterConfigurator*)> &cb) -> int;
  void check_integrity();

protected:
  struct PipelineConfig {
    int index;
    pjs::Ref<pjs::Function> on_start;
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
  void read(const std::string &pathname);
  void task(const std::string &when);
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
    std::string ip;
    int port;
    Listener::Options options;
  };

  struct ReaderConfig : public PipelineConfig {
    std::string pathname;
  };

  struct TaskConfig : public PipelineConfig {
    std::string name;
    std::string when;
  };

  struct NamedPipelineConfig : public PipelineConfig {
    std::string name;
  };

  pjs::Ref<pjs::Object> m_context_prototype;
  pjs::Ref<pjs::Class> m_context_class;
  std::list<Export> m_exports;
  std::list<Import> m_imports;
  std::list<ListenConfig> m_listens;
  std::list<ReaderConfig> m_readers;
  std::list<TaskConfig> m_tasks;
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
