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

//
// Configuration
//

class Configuration : public pjs::ObjectTemplate<Configuration> {
public:
  static void set_reuse_port(bool b) { s_reuse_port = b; }

  void connect(const pjs::Value &target, pjs::Object *options);
  void listen(int port, pjs::Object *options);
  void task();
  void task(double interval);
  void task(const std::string &interval);
  void pipeline(const std::string &name);

  void decode_dubbo();
  void decode_http_request();
  void decode_http_response(bool bodiless);
  void decompress_body(pjs::Str *algorithm);
  void demux(pjs::Str *target);
  void dummy();
  void dump(const pjs::Value &tag);
  void encode_dubbo(pjs::Object *message_obj);
  void encode_http_request(pjs::Object *request_obj);
  void encode_http_response(pjs::Object *response_obj);
  void exec(const pjs::Value &command);
  void fork(pjs::Str *target, pjs::Object *session_data);
  void link(size_t count, pjs::Str **targets, pjs::Function **conditions);
  void merge(pjs::Str *target, pjs::Function *selector);
  void mux(pjs::Str *target, pjs::Function *selector);
  void on_body(pjs::Function *callback);
  void on_event(Event::Type type, pjs::Function *callback);
  void on_message(pjs::Function *callback);
  void on_start(pjs::Function *callback);
  void pack(int batch_size, pjs::Object *options);
  void print();
  void proxy_socks(pjs::Str *target, pjs::Function *on_connect);
  void proxy_socks4(pjs::Str *target, pjs::Function *on_connect);
  void replace_body(const pjs::Value &replacement);
  void replace_event(Event::Type type, const pjs::Value &replacement);
  void replace_message(const pjs::Value &replacement);
  void replace_start(const pjs::Value &replacement);
  void tap(const pjs::Value &quota, const pjs::Value &account);
  void use(Module *module, pjs::Str *pipeline, pjs::Object *argv);
  void wait(pjs::Function *condition);

  void apply(Module *module);
  void draw(Graph &g);

private:
  Configuration(pjs::Object *context_prototype);

  struct ListenConfig {
    std::string ip;
    int port;
    bool reuse;
    bool ssl;
    asio::ssl::context ssl_context;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct TaskConfig {
    std::string name;
    std::string interval;
    std::list<std::unique_ptr<Filter>> filters;
  };

  struct NamedPipelineConfig {
    std::string name;
    std::list<std::unique_ptr<Filter>> filters;
  };

  pjs::Ref<pjs::Class> m_context_class;
  std::list<ListenConfig> m_listens;
  std::list<TaskConfig> m_tasks;
  std::list<NamedPipelineConfig> m_named_pipelines;
  std::list<std::unique_ptr<Filter>> *m_current_filters = nullptr;

  void append_filter(Filter *filter);

  static bool s_reuse_port;

  friend class pjs::ObjectTemplate<Configuration>;
};

} // namespace pipy

#endif // API_CONFIGURATION_HPP
