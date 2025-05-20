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

#include "version.h"

#include "api/crypto.hpp"
#include "api/logging.hpp"
#include "api/pipy.hpp"
#include "api/stats.hpp"
#include "codebase.hpp"
#include "codebase-store.hpp"
#include "fs.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "input.hpp"
#include "listener.hpp"
#include "main-options.hpp"
#include "net.hpp"
#include "os-platform.hpp"
#include "pipeline.hpp"
#include "status.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "worker.hpp"
#include "worker-thread.hpp"

#include <signal.h>

#include <list>
#include <string>
#include <tuple>

#include <openssl/opensslv.h>
#include <openssl/conf.h>

using namespace pipy;

//
// Show version
//

static void show_version() {
  std::cout << "Version          : " << PIPY_VERSION << std::endl;
  std::cout << "Commit           : " << PIPY_COMMIT << std::endl;
  std::cout << "Commit Date      : " << PIPY_COMMIT_DATE << std::endl;
  std::cout << "Host             : " << PIPY_HOST << std::endl;
#ifdef PIPY_USE_NTLS
  std::cout << "Tongsuo          : " << TONGSUO_VERSION_TEXT << std::endl;
#else
  std::cout << "OpenSSL          : " << OPENSSL_VERSION_TEXT << std::endl;
  std::cout << "OpenSSL Conf     : " << CONF_get1_default_config_file() << std::endl;
#endif

#ifdef PIPY_DEFAULT_OPTIONS
  std::cout << "Default Options  : " << PIPY_DEFAULT_OPTIONS << std::endl;
#endif

  std::cout << "Builtin Codebases: " << std::endl;
  for (const auto &name : Codebase::list_builtin()) {
    std::cout << "  " << name << std::endl;
  }
}

//
// Admin service
//

class AdminService {
public:
  bool is_open() const { return m_port > 0; }
  void open(const std::string &ip, int port, WorkerThread *wt);
  void open(const std::string &ip, int port, WorkerManager *wm);
  void close();

private:
  std::string m_ip;
  int m_port = 0;

  int open(const std::string &ip, int port, const std::function<pjs::Object*(const std::string &path)> &handler);

  pjs::Object* respond_log(const std::string &path);
  pjs::Object* respond_dump(const std::string &path, Status &status);
  pjs::Object* respond_dump_objects(const std::map<std::string, size_t> &counts);

  static const std::string s_path_log;
  static const std::string s_path_dump;
  static const std::string s_prefix_log;
  static const std::string s_prefix_dump;
  static const std::string s_prefix_dump_objects;

  static Data::Producer s_dp;
};

const std::string AdminService::s_path_log("/log");
const std::string AdminService::s_path_dump("/dump");
const std::string AdminService::s_prefix_log("/log/");
const std::string AdminService::s_prefix_dump("/dump/");
const std::string AdminService::s_prefix_dump_objects("/dump/objects/");
Data::Producer AdminService::s_dp("Admin");

void AdminService::open(const std::string &ip, int port, WorkerThread *wt) {
  if (!is_open()) {
    m_ip = ip;
    m_port = open(ip, port, [=](const std::string &path) -> pjs::Object* {
      auto net = &Net::current();
      if (path == s_path_log || utils::starts_with(path, s_prefix_log)) {
        return respond_log(path);
      } else if (utils::starts_with(path, s_prefix_dump_objects)) {
        auto class_name = path.substr(s_prefix_dump_objects.length());
        if (!class_name.empty()) {
          auto promise = pjs::Promise::make();
          auto counts = new std::map<std::string, size_t>;
          wt->dump_objects(class_name, *counts, [=]() {
            net->post([=]() {
              InputContext ic;
              promise->settle(true, respond_dump_objects(*counts));
              promise->release();
              delete counts;
            });
          });
          promise->retain();
          return promise;
        }
      } else if (path == s_path_dump || utils::starts_with(path, s_prefix_dump)) {
        auto promise = pjs::Promise::make();
        auto status = new Status;
        wt->status(*status, [=]() {
          net->post([=]() {
            InputContext ic;
            promise->settle(true, respond_dump(path, *status));
            promise->release();
            delete status;
          });
        });
        promise->retain();
        return promise;
      }
      return nullptr;
    });
  }
}

void AdminService::open(const std::string &ip, int port, WorkerManager *wm) {
  if (!is_open()) {
    m_ip = ip;
    m_port = open(ip, port, [=](const std::string &path) -> pjs::Object* {
      if (path == s_path_log || utils::starts_with(path, s_prefix_log)) {
        return respond_log(path);
      } else if (utils::starts_with(path, s_prefix_dump_objects)) {
        auto class_name = path.substr(s_prefix_dump_objects.length());
        if (!class_name.empty()) {
          auto promise = pjs::Promise::make();
          wm->dump_objects(class_name, [=](const std::map<std::string, size_t> &counts) {
            InputContext ic;
            promise->settle(true, respond_dump_objects(counts));
            promise->release();
          });
          promise->retain();
          return promise;
        }
      } else if (path == s_path_dump || utils::starts_with(path, s_prefix_dump)) {
        auto promise = pjs::Promise::make();
        wm->status([=](Status &status) {
          InputContext ic;
          promise->settle(true, respond_dump(path, status));
          promise->release();
        });
        promise->retain();
        return promise;
      }
      return nullptr;
    });
  }
}

int AdminService::open(const std::string &ip, int port, const std::function<pjs::Object*(const std::string &path)> &handler) {
  PipelineLayout *ppl = PipelineLayout::make(Worker::current());
  ppl->append(
    new http::Server(
      pjs::Function::make(pjs::Method::make(
        "admin-handler",
        [=](pjs::Context &context, pjs::Object*, pjs::Value &ret) {
          auto req = context.arg(0).as<Message>();
          auto res = handler(req->head()->as<http::RequestHead>()->path->str());
          if (res) {
            ret.set(res);
          } else {
            auto head = http::ResponseHead::make();
            head->status = 404;
            ret.set(Message::make(head, nullptr));
          }
        }
      )),
      http::Server::Options()
    )
  );

  Log::info("[admin] Try to start admin on port [%s]:%d", ip.c_str(), port);

  Listener::Options opts;
  while (port < 65536) {
    auto listener = Listener::get(Port::Protocol::TCP, ip, port);
    if (!listener->is_open()) {
      try {
        listener->set_options(opts);
        listener->pipeline_layout(ppl);
        Log::info("[admin] Started admin on port [%s]:%d", ip.c_str(), port);
        return port;
      } catch (std::runtime_error &err) {
        Log::error("[admin] Cannot start admin on port [%s]:%d: %s", ip.c_str(), port, err.what());
      }
    } else {
      Log::error("[admin] Cannot start admin on port [%s]:%d because it's currenly busy", ip.c_str(), port);
    }
    port++;
  }

  Log::error("[admin] Unable to find any port for admin");
  return 0;
}

void AdminService::close() {
  if (is_open()) {
    auto listener = Listener::get(Port::Protocol::TCP, m_ip, m_port);
    listener->pipeline_layout(nullptr);
    Log::info("[admin] Stopped admin on port [%s]:%d", m_ip.c_str(), m_port);
    m_port = 0;
  }
}

pjs::Object* AdminService::respond_log(const std::string &path) {
  static const std::string s_pipy_log("pipy_log");
  auto name = path == s_path_log ? s_pipy_log : path.substr(s_prefix_log.length());
  Data buf;
  logging::Logger::tail(s_pipy_log, buf);
  return Message::make(Data::make(std::move(buf)));
}

pjs::Object* AdminService::respond_dump(const std::string &path, Status &status) {
  Data buf;
  Data::Builder db(buf, &s_dp);
  if (path == s_path_dump) {
    status.dump_pools(db);
    status.dump_objects(db);
    status.dump_chunks(db);
    status.dump_buffers(db);
    status.dump_inbound(db);
    status.dump_outbound(db);
  } else {
    auto name = path.substr(s_prefix_dump.length());
    if (name == "pools") {
      status.dump_pools(db);
    } else if (name == "objects") {
      status.dump_objects(db);
    } else if (name == "chunks") {
      status.dump_chunks(db);
    } else if (name == "buffers") {
      status.dump_buffers(db);
    } else if (name == "io") {
      status.dump_inbound(db);
      status.dump_outbound(db);
    } else if (name == "in") {
      status.dump_inbound(db);
    } else if (name == "out") {
      status.dump_outbound(db);
    } else {
      return nullptr;
    }
  }
  db.flush();
  return Message::make(Data::make(std::move(buf)));
}

pjs::Object* AdminService::respond_dump_objects(const std::map<std::string, size_t> &counts) {
  Data buf;
  Data::Builder db(buf, &s_dp);
  for (const auto &p : counts) {
    char str[100];
    auto len = std::snprintf(str, sizeof(str), "%llu ", (unsigned long long)p.second);
    db.push(str, len);
    db.push(p.first);
    db.push('\n');
  }
  db.flush();
  return Message::make(Data::make(std::move(buf)));
}

//
// Handle signals
//

class SignalHandler {
public:
  SignalHandler() {}

  SignalHandler(WorkerThread *worker_thread, const std::string &admin_ip = "", int admin_port = 0, bool admin_open = false) {
    bool started = false;
    std::condition_variable start_cv;
    std::mutex start_cv_mutex;
    std::unique_lock<std::mutex> lock(start_cv_mutex);

    m_thread = std::thread(
      [=, &started, &start_cv, &start_cv_mutex]() {
        m_net = &Net::current();
        {
          std::lock_guard<std::mutex> lock(start_cv_mutex);
          started = true;
          start_cv.notify_one();
        }
        run(
          m_net, admin_ip, admin_port, admin_open,
          [=](int sig) {
            if (sig == SIGNAL_ADMIN && admin_port) {
              if (m_admin.is_open()) {
                m_admin.close();
              } else {
                m_admin.open(admin_ip, admin_port, worker_thread);
              }
            } else {
              worker_thread->signal(sig);
            }
          }
        );
      }
    );

    start_cv.wait(lock, [&]() { return started; });
  }

  ~SignalHandler() {
    if (m_net) m_net->stop();
    if (m_thread.joinable()) m_thread.join();
  }

  void run(WorkerManager *worker_manager, const std::string &admin_ip = "", int admin_port = 0, bool admin_open = false) {
    run(
      &Net::current(), admin_ip, admin_port, admin_open,
      [=](int sig) {
        if (sig == SIGNAL_ADMIN && admin_port) {
          if (m_admin.is_open()) {
            m_admin.close();
          } else {
            m_admin.open(admin_ip, admin_port, worker_manager);
          }
        } else {
          worker_manager->signal(sig);
        }
      }
    );
  }

private:
  std::thread m_thread;
  Net* m_net = nullptr;
  AdminService m_admin;

  void run(Net *net, const std::string &admin_ip, int admin_port, bool admin_open, const std::function<void(int)> &cb) {
    Log::init();
    pjs::Ref<Worker> worker = Worker::make(pjs::Promise::Period::current());
    asio::signal_set signals(Net::context());
    signals.add(SIGNAL_STOP);
    signals.add(SIGNAL_ADMIN);
    std::function<void()> wait;
    wait = [&]() {
      signals.async_wait(
        [&](const std::error_code &ec, int sig) {
          if (ec != asio::error::operation_aborted) {
            if (!ec) {
              cb(sig);
            }
            wait();
          }
        }
      );
    };
    wait();
    worker->start();
    if (admin_open) cb(SIGNAL_ADMIN);
    net->run();
    worker->stop(true);
    worker = nullptr;
    Log::shutdown();
    Listener::delete_all();
    Timer::cancel_all();
  }
};

//
// JSON arguments
//

static void json_args_append(std::string &json, const char *key, int value) {
  json += json.empty() ? '{' : ',';
  json += '"';
  json += key;
  json += "\":";
  json += std::to_string(value);
}

static void json_args_append(std::string &json, const char *key, const std::string &value) {
  json += json.empty() ? '{' : ',';
  json += '"';
  json += key;
  json += "\":\"";
  json += utils::escape(value);
  json += '"';
}

//
// Program entrance
//

extern "C" PIPY_API
int pipy_main(int argc, char *argv[]) {

  int exit_code = 0;

  try {
    auto &opts = MainOptions::global();
    opts.parse(argc, argv);

    if (opts.version) {
      show_version();
      return 0;
    }

    if (opts.help) {
      MainOptions::show_help();
      return 0;
    }

    Status::LocalInstance::since = utils::now();
    Status::LocalInstance::source = opts.filename;
    Status::LocalInstance::name = opts.instance_name;

    if (opts.instance_uuid.empty()) {
      Status::LocalInstance::uuid = utils::make_uuid_v4();
    } else {
      Status::LocalInstance::uuid = opts.instance_uuid;
    }

    os::init();
    Net::init();
    Log::set_filename(opts.log_file);
    Log::set_rotate(opts.log_file_rotate_interval, opts.log_file_max_size, opts.log_file_max_count);
    Log::set_level(opts.log_level);
    Log::set_topics(opts.log_topics);
    Log::set_local_output(opts.log_local);
    Log::set_local_only(opts.log_local_only);
    Log::init();
    logging::Logger::set_history_size(opts.log_history_limit);
    Listener::set_reuse_port(opts.reuse_port);
    pjs::Class::set_tracing(opts.trace_objects);
    pjs::Math::init();
    crypto::Crypto::init(opts.openssl_engine);
    tls::TLSSession::init();

    std::string admin_ip("::");
    int admin_port = 6060; // default repo port
    bool admin_open = (!opts.admin_port.empty() && !opts.admin_port_off);
    auto admin_ip_port = opts.admin_port;
    if (!admin_ip_port.empty()) {
      if (!utils::get_host_port(admin_ip_port, admin_ip, admin_port)) {
        admin_port = std::atoi(admin_ip_port.c_str());
      }
      if (admin_ip.empty()) admin_ip = "::";
    }

    bool is_repo = false;
    bool is_repo_proxy = false;
    bool is_remote = false;
    bool is_builtin = false;
    bool is_tls = false;
    bool is_file = false;
    bool is_file_found = false;

    if (!opts.eval) {
      if (opts.filename.empty()) {
        is_repo = true;

      } else if (utils::starts_with(opts.filename, "repo://")) {
        is_builtin = true;

      } else if (utils::starts_with(opts.filename, "http://")) {
        is_remote = true;

      } else if (utils::starts_with(opts.filename, "https://")) {
        is_remote = true;
        is_tls = true;

      } else {
        is_file = true;
        auto full_path = fs::abs_path(opts.filename);
        if (fs::exists(full_path)) {
          is_file_found = true;
          is_repo = fs::is_dir(full_path);
          opts.filename = full_path;
        } else if (opts.file) {
          std::string msg("file or directory does not exist: ");
          throw std::runtime_error(msg + full_path);
        }
      }
    }

    if (is_remote) {
      auto i = opts.filename.find('/');
      auto target = opts.filename.substr(i+2);
      if (!target.empty() && target.back() == '/') {
        target.resize(target.size() - 1);
      }
      if (utils::is_host_port(target)) {
        opts.filename = target;
        is_remote = false;
        is_repo_proxy = true;
      }
    }

    if (!is_repo) {
      if (!opts.init_repo.empty()) throw std::runtime_error("invalid option --init-repo for non-repo mode");
      if (!opts.init_code.empty()) throw std::runtime_error("invalid option --init-code for non-repo mode");
    }

    Codebase *codebase = nullptr;
    std::vector<std::string> args = opts.arguments;

    // Start as codebase repo service
    if (is_repo) {
      codebase = Codebase::from_builtin("/pipy/repo");
      std::string json_args;
      json_args_append(json_args, "pathname", opts.filename);
      json_args_append(json_args, "listen", '[' + admin_ip + "]:" + std::to_string(admin_port));
      json_args += '}';
      args.insert(args.begin() + 1, json_args);

    // Start as codebase repo proxy
    } else if (is_repo_proxy) {
      codebase = Codebase::from_builtin("/pipy/repo-proxy");
      std::string json_args;
      json_args_append(json_args, "url", opts.filename);
      json_args += '}';
      args.insert(args.begin() + 1, json_args);

    // Start using a remote codebase
    } else if (is_remote) {
      codebase = Codebase::from_builtin("/pipy/worker");
      std::string json_args;
      json_args_append(json_args, "url", opts.filename);
      json_args_append(json_args, "threads", opts.threads);
      json_args += '}';
      args.insert(args.begin() + 1, json_args);

    // Start using a builtin codebase
    } else if (is_builtin) {
      auto name = opts.filename.substr(6);
      codebase = Codebase::from_builtin(name);

    // Start using a local codebase
    } else if (is_file_found) {
      codebase = Codebase::from_fs(opts.filename);

    // Start using a one-liner
    } else {
      codebase = Codebase::from_fs(fs::abs_path("."), opts.filename);
    }

    codebase = Codebase::from_root(codebase);

    if (opts.threads > 1 && !is_repo && !is_repo_proxy) {
      pjs::Ref<WorkerManager> wm = WorkerManager::make(
        codebase, opts.threads,
        []() { Net::current().stop(); }
      );
      wm->start(args);
      SignalHandler().run(wm, admin_ip, admin_port, admin_open);

    } else {
      WorkerThread t(codebase);
      SignalHandler sh(&t, admin_ip, admin_port, admin_open);
      t.main(args);
    }

    delete codebase;

    crypto::Crypto::free();
    stats::Metric::local().clear();
    Log::shutdown();
    logging::Logger::close_all();
    Timer::cancel_all();
    os::cleanup();

  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return exit_code;
}

#ifdef PIPY_SHARED

extern "C" PIPY_API
void pipy_exit(int force) {
  if (auto wt = WorkerThread::current()) {
    wt->stop(force);
  } else if (auto wm = WorkerManager::current()) {
    wm->stop(force);
  }
}

#else // !PIPY_SHARED

int main(int argc, char *argv[]) {
  return pipy_main(argc, argv);
}

#endif // PIPY_SHARED
