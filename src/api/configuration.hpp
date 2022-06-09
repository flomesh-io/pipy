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

#include <list>
#include <map>
#include <memory>

namespace pipy {

class Configuration;
class Filter;
class Graph;
class Module;
class Worker;

//
// FilterConfigurator
//

class FilterConfigurator : public pjs::ObjectTemplate<FilterConfigurator> {
public:
  void accept_http_tunnel(pjs::Function *handler);
  void accept_socks(pjs::Function *on_connect);
  void accept_tls(pjs::Object *options);
  void compress_http(pjs::Object *options);
  void compress_message(pjs::Object *options);
  void connect(const pjs::Value &target, pjs::Object *options);
  void connect_http_tunnel(const pjs::Value &address);
  void connect_socks(const pjs::Value &address);
  void connect_tls(pjs::Object *options);
  void decode_dubbo();
  void decode_http_request();
  void decode_http_response(pjs::Object *options);
  void decode_mqtt(pjs::Object *options);
  void decode_websocket();
  void decompress_http(pjs::Function *enable);
  void decompress_message(const pjs::Value &algorithm);
  void deframe(pjs::Object *states);
  void deposit_message(const pjs::Value &filename, pjs::Object *options);
  void detect_protocol(pjs::Function *callback);
  void demux();
  void demux_queue();
  void demux_http(pjs::Object *options);
  void dummy();
  void dump(const pjs::Value &tag);
  void encode_dubbo(pjs::Object *message_obj);
  void encode_http_request(pjs::Object *options);
  void encode_http_response(pjs::Object *options);
  void encode_mqtt();
  void encode_websocket();
  void exec(const pjs::Value &command);
  void fork(pjs::Object *initializers);
  void input(pjs::Function *callback);
  void link(size_t count, pjs::Str **targets, pjs::Function **conditions);
  void merge(pjs::Function *group, pjs::Object *options);
  void mux(pjs::Function *group, pjs::Object *options);
  void mux_queue(pjs::Function *group, pjs::Object *options);
  void mux_http(pjs::Function *group, pjs::Object *options);
  void on_body(pjs::Function *callback, int size_limit);
  void on_event(Event::Type type, pjs::Function *callback);
  void on_message(pjs::Function *callback, int size_limit);
  void on_start(pjs::Function *callback);
  void on_tls_client_hello(pjs::Function *callback);
  void output(pjs::Function *output_f);
  void pack(int batch_size, pjs::Object *options);
  void print();
  void replace_body(const pjs::Value &replacement, int size_limit);
  void replace_event(Event::Type type, const pjs::Value &replacement);
  void replace_message(const pjs::Value &replacement, int size_limit);
  void replace_start(const pjs::Value &replacement);
  void serve_http(pjs::Object *handler);
  void split(pjs::Function *callback);
  void tee(const pjs::Value &filename);
  void throttle_concurrency(const pjs::Value &quota, const pjs::Value &account);
  void throttle_data_rate(const pjs::Value &quota, const pjs::Value &account);
  void throttle_message_rate(const pjs::Value &quota, const pjs::Value &account);
  void use(Module *module, pjs::Str *pipeline);
  void use(const std::list<Module*> modules, pjs::Str *pipeline, pjs::Function *when);
  void use(const std::list<Module*> modules, pjs::Str *pipeline, pjs::Str *pipeline_down, pjs::Function *when);
  void wait(pjs::Function *condition, pjs::Object *options);

  void to(pjs::Str *layout_name);
  void to(const std::string &name, const std::function<void(FilterConfigurator*)> &cb);
  void check_integrity();

protected:
  FilterConfigurator(
    Configuration *configuration,
    std::list<std::unique_ptr<Filter>> *filters = nullptr
  ) : m_configuration(configuration)
    , m_filters(filters) {}

  void set_filter_list(std::list<std::unique_ptr<Filter>> *filters) {
    m_filters = filters;
  }

private:
  auto append_filter(Filter *filter) -> Filter*;
  void require_sub_pipeline(Filter *filter);

  Configuration* m_configuration;
  std::list<std::unique_ptr<Filter>> *m_filters;
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

  void bind_pipelines();
  void bind_exports(Worker *worker, Module *module);
  void bind_imports(Worker *worker, Module *module, pjs::Expr::Imports *imports);
  void apply(Module *module);
  void draw(Graph &g);

private:
  Configuration(pjs::Object *context_prototype);

  struct ListenConfig {
    int index;
    std::string ip;
    int port;
    Listener::Options options;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct ReaderConfig {
    int index;
    std::string pathname;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct TaskConfig {
    int index;
    std::string name;
    std::string when;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct NamedPipelineConfig {
    int index;
    std::string name;
    std::list<std::unique_ptr<Filter>> filters;
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
  int m_current_pipeline_index = 0;

  auto next_pipeline_index() -> int { return m_current_pipeline_index++; }
  auto new_indexed_pipeline(const std::string &name, int &index) -> FilterConfigurator*;

  friend class pjs::ObjectTemplate<Configuration, FilterConfigurator>;
  friend class FilterConfigurator;
};

} // namespace pipy

#endif // API_CONFIGURATION_HPP
