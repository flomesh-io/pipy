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
#include "pjs/pjs.hpp"
#include "event.hpp"

#include <list>
#include <map>
#include <memory>

namespace pipy {

class Filter;
class Graph;
class Module;
class Worker;

//
// Configuration
//

class Configuration : public pjs::ObjectTemplate<Configuration> {
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
  void task(const std::string &when);
  void pipeline(const std::string &name);

  void accept_socks(pjs::Str *target, pjs::Function *on_connect);
  void accept_tls(pjs::Str *target, pjs::Object *options);
  void connect(const pjs::Value &target, pjs::Object *options);
  void connect_tls(pjs::Str *target, pjs::Object *options);
  void decode_dubbo();
  void decode_http_request();
  void decode_http_response(pjs::Object *options);
  void decompress_http(pjs::Function *enable);
  void decompress_message(const pjs::Value &algorithm);
  void demux(pjs::Str *target);
  void demux_http(pjs::Str *target, pjs::Object *options);
  void dummy();
  void dump(const pjs::Value &tag);
  void encode_dubbo(pjs::Object *message_obj);
  void encode_http_request();
  void encode_http_response(pjs::Object *options);
  void exec(const pjs::Value &command);
  void fork(pjs::Str *target, pjs::Object *initializers);
  void link(size_t count, pjs::Str **targets, pjs::Function **conditions);
  void merge(pjs::Str *target, const pjs::Value &key);
  void mux(pjs::Str *target, const pjs::Value &key);
  void mux_http(pjs::Str *target, const pjs::Value &key);
  void on_body(pjs::Function *callback, int size_limit);
  void on_event(Event::Type type, pjs::Function *callback);
  void on_message(pjs::Function *callback, int size_limit);
  void on_start(pjs::Function *callback);
  void pack(int batch_size, pjs::Object *options);
  void print();
  void replace_body(const pjs::Value &replacement, int size_limit);
  void replace_event(Event::Type type, const pjs::Value &replacement);
  void replace_message(const pjs::Value &replacement, int size_limit);
  void replace_start(const pjs::Value &replacement);
  void serve_http(pjs::Object *handler);
  void split(pjs::Function *callback);
  void throttle_data_rate(const pjs::Value &quota, const pjs::Value &account);
  void throttle_message_rate(const pjs::Value &quota, const pjs::Value &account);
  void use(Module *module, pjs::Str *pipeline);
  void use(const std::list<Module*> modules, pjs::Str *pipeline, pjs::Function *when);
  void use(const std::list<Module*> modules, pjs::Str *pipeline, pjs::Str *pipeline_down, pjs::Function *when);
  void wait(pjs::Function *condition);

  void bind_pipelines();
  void bind_exports(Worker *worker, Module *module);
  void bind_imports(Worker *worker, Module *module, pjs::Expr::Imports *imports);
  void apply(Module *module);
  void draw(Graph &g);

private:
  Configuration(pjs::Object *context_prototype);

  struct ListenConfig {
    std::string ip;
    int port;
    int max_connections;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct TaskConfig {
    std::string name;
    std::string when;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct NamedPipelineConfig {
    std::string name;
    std::list<std::unique_ptr<Filter>> filters;
  };

  pjs::Ref<pjs::Object> m_context_prototype;
  pjs::Ref<pjs::Class> m_context_class;
  std::list<Export> m_exports;
  std::list<Import> m_imports;
  std::list<ListenConfig> m_listens;
  std::list<TaskConfig> m_tasks;
  std::list<NamedPipelineConfig> m_named_pipelines;
  std::list<std::unique_ptr<Filter>> *m_current_filters = nullptr;

  void append_filter(Filter *filter);

  friend class pjs::ObjectTemplate<Configuration>;
};

} // namespace pipy

#endif // API_CONFIGURATION_HPP