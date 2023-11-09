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

#ifndef ADMIN_SERVICE_HPP
#define ADMIN_SERVICE_HPP

#include "api/http.hpp"
#include "api/crypto.hpp"
#include "api/stats.hpp"
#include "filter.hpp"
#include "module.hpp"
#include "context.hpp"
#include "message.hpp"
#include "data.hpp"
#include "tar.hpp"
#include "codebase-store.hpp"
#include "status.hpp"
#include "timer.hpp"

#include <chrono>
#include <map>
#include <set>
#include <vector>

namespace pipy {

//
// AdminService
//

class AdminService : public pjs::RefCount<AdminService> {
public:
  struct Options {
    pjs::Ref<crypto::Certificate> cert;
    pjs::Ref<crypto::PrivateKey> key;
    std::vector<pjs::Ref<crypto::Certificate>> trusted;
  };

  AdminService(CodebaseStore *store, const std::string &gui_files = std::string());
  ~AdminService();

  void open(const std::string &ip, int port, const Options &options);
  void close();
  void write_log(const std::string &name, const Data &data);

private:
  enum { METRIC_HISTORY_SIZE = 60 };

  //
  // AdminService::WebSocketHandler
  //

  class WebSocketHandler : public Filter {
  public:
    WebSocketHandler(AdminService *service)
      : m_service(service) {}

    WebSocketHandler(const WebSocketHandler &r)
      : m_service(r.m_service) {}

    void log_enable(const std::string &name, bool enabled);
    void log_tail(const std::string &name);
    void log_broadcast(const Data &data);
    void signal_reload();

  private:
    virtual auto clone() -> Filter* override;
    virtual void reset() override;
    virtual void process(Event *evt) override;
    virtual void dump(Dump &d) override;

    pjs::Ref<AdminService> m_service;
    Data m_payload;
    bool m_started;
  };

  //
  // AdminService::LogWatcher
  //

  class LogWatcher {
  public:
    LogWatcher(AdminService *service, const std::string &uuid, const std::string &name);
    ~LogWatcher();

    void set_handler(WebSocketHandler *handler);
    void start(const Data &data);
    void send(const Data &data);

  private:
    pjs::Ref<AdminService> m_service;
    std::string m_uuid;
    std::string m_name;
    WebSocketHandler *m_handler = nullptr;
    bool m_started = false;
  };

  //
  // AdminService::Context
  //

  class Context : public pjs::ContextTemplate<Context, pipy::Context> {
  public:

    ~Context() { delete log_watcher; }
    std::string instance_uuid;
    std::string log_name;
    LogWatcher* log_watcher = nullptr;
    bool is_admin_link = false;
  };

  //
  // AdminService::Module
  //

  class Module : public ModuleBase {
  public:
    Module() : ModuleBase("AdminService") {}
    virtual auto new_context(pipy::Context *base) -> pipy::Context* override {
      return Context::make();
    }
  };

  //
  // AdminService::Instance
  //

  struct Instance {
    int index;
    double timestamp;
    std::string ip;
    std::string codebase_name;
    Status status;
    stats::MetricData metric_data;
    stats::MetricHistory metric_history;
    WebSocketHandler* admin_link = nullptr;
    std::map<std::string, std::set<LogWatcher*>> log_watchers;
    Instance() : metric_history(METRIC_HISTORY_SIZE) {}
  };

  std::string m_ip;
  int m_port;
  int m_last_instance_index = 0;
  pjs::Ref<pjs::Method> m_handler_method;
  CodebaseStore* m_store;
  std::string m_current_codebase;
  std::string m_current_program;
  std::map<int, Instance*> m_instances;
  std::map<std::string, int> m_instance_map;
  std::map<std::string, std::set<int>> m_codebase_instances;
  std::map<std::string, std::set<LogWatcher*>> m_local_log_watchers;
  stats::MetricHistory m_local_metric_history;
  Timer m_metrics_history_timer;
  Timer m_inactive_instance_removal_timer;
  std::chrono::time_point<std::chrono::steady_clock> m_metrics_timestamp;

  Tarball m_www_files;
  std::map<std::string, pjs::Ref<http::File>> m_www_file_cache;
  pjs::Ref<http::Directory> m_gui_files;

  pjs::Ref<http::ResponseHead> m_response_head_text;
  pjs::Ref<http::ResponseHead> m_response_head_json;
  pjs::Ref<http::ResponseHead> m_response_head_text_gzip;
  pjs::Ref<http::ResponseHead> m_response_head_json_gzip;
  pjs::Ref<Message> m_response_ok;
  pjs::Ref<Message> m_response_created;
  pjs::Ref<Message> m_response_deleted;
  pjs::Ref<Message> m_response_not_found;
  pjs::Ref<Message> m_response_method_not_allowed;
  pjs::Ref<Message> m_response_upgraded_ws;

  pjs::Ref<Module> m_module;

  auto handle(Context *ctx, Message *req) -> pjs::Object*;

  Message* dump_GET();
  Message* dump_GET(const std::string &path);
  Message* log_GET();
  Message* log_GET(const std::string &path);
  Message* metrics_GET(pjs::Object *headers);

  Message* repo_HEAD(const std::string &path);
  Message* repo_GET(const std::string &path);
  Message* repo_POST(Context *ctx, const std::string &path, Data *data);

  Message* api_v1_repo_GET(const std::string &path);
  Message* api_v1_repo_POST(const std::string &path, Data *data);
  Message* api_v1_repo_PATCH(const std::string &path, Data *data);
  Message* api_v1_repo_DELETE(const std::string &path);

  Message* api_v1_repo_files_GET(const std::string &path);
  Message* api_v1_repo_files_POST(const std::string &path, Data *data);
  Message* api_v1_repo_files_PATCH(const std::string &path, Data *data);
  Message* api_v1_repo_files_DELETE(const std::string &path);

  Message* api_v1_files_GET(const std::string &path);
  Message* api_v1_files_POST(const std::string &path, Data *data);
  Message* api_v1_files_PATCH(const std::string &path, Data *data);
  Message* api_v1_files_DELETE(const std::string &path);

  Message* api_v1_program_GET();
  Message* api_v1_program_POST(Data *data);
  Message* api_v1_program_PATCH(Data *data);
  Message* api_v1_program_DELETE();

  Message* api_v1_status_GET();
  Message* api_v1_metrics_GET(const std::string &uuid);

  Message* api_v1_graph_POST(Data *data);

  Message* response(const Data &text);
  Message* response(const std::string &text);
  Message* response(const std::set<std::string> &list);
  Message* response(int status_code, const std::string &message);

  auto codebase_of(const std::string &path) -> CodebaseStore::Codebase*;
  auto codebase_of(const std::string &path, std::string &filename) -> CodebaseStore::Codebase*;
  auto get_instance(const std::string &uuid) -> Instance*;
  auto get_instance(int index) -> Instance*;

  static auto response_head(
    int status,
    const std::map<std::string, std::string> &headers
  ) -> http::ResponseHead*;

  void on_watch_start(Context *ctx, const std::string &path);
  void on_log(Context *ctx, const std::string &name, const Data &data);
  void on_log_tail(Context *ctx, const std::string &name, const Data &data);
  void on_metrics(Context *ctx, const Data &data);

  auto change_program(const std::string &path, bool reload) -> Message*;
  void metrics_history_step();
  void inactive_instance_removal_step();
};

} // namespace pipy

#endif // ADMIN_SERVICE_HPP
