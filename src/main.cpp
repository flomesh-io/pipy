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

#include "admin-link.hpp"
#include "admin-service.hpp"
#include "admin-proxy.hpp"
#include "api/crypto.hpp"
#include "api/logging.hpp"
#include "api/pipy.hpp"
#include "api/stats.hpp"
#include "codebase.hpp"
#include "fs.hpp"
#include "filters/tls.hpp"
#include "input.hpp"
#include "listener.hpp"
#include "main-options.hpp"
#include "net.hpp"
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

using namespace pipy;

static AdminService *s_admin = nullptr;
static AdminProxy *s_admin_proxy = nullptr;
static AdminLink *s_admin_link = nullptr;
static std::string s_admin_ip;
static int s_admin_port = 0;
static AdminService::Options s_admin_options;
static bool s_has_shutdown = false;

//
// Show version
//

static void show_version() {
  std::cout << "Version     : " << PIPY_VERSION << std::endl;
  std::cout << "Commit      : " << PIPY_COMMIT << std::endl;
  std::cout << "Commit Date : " << PIPY_COMMIT_DATE << std::endl;
  std::cout << "Host        : " << PIPY_HOST << std::endl;
  std::cout << "OpenSSL     : " << OPENSSL_VERSION_TEXT << std::endl;

#ifdef PIPY_USE_GUI
  std::cout << "Builtin GUI : " << "Yes" << std::endl;
#else
  std::cout << "Builtin GUI : " << "No" << std::endl;
#endif

#ifdef PIPY_USE_SAMPLES
  std::cout << "Samples     : " << "Yes" << std::endl;
#else
  std::cout << "Samples     : " << "No" << std::endl;
#endif
}

//
// Reload codebase
//

static void reload_codebase(bool force) {
  if (auto *codebase = Codebase::current()) {
    codebase->sync(
      force, [](bool ok) {
        if (ok) {
          WorkerManager::get().reload();
        }
      }
    );
  }
}

//
// Establish admin link
//

static void start_admin_link(const std::string &url, const AdminLink::TLSSettings *tls_settings) {
  std::string url_path = url;
  if (url_path.back() != '/') url_path += '/';
  url_path += Status::LocalInstance::uuid;
  s_admin_link = new AdminLink(url_path, tls_settings);
  s_admin_link->add_handler(
    [](const std::string &command, const Data &) {
      if (command == "reload") {
        reload_codebase(true);
        return true;
      } else {
        return false;
      }
    }
  );
  logging::Logger::set_admin_link(s_admin_link);
}

//
// Open/close admin port
//

static void toggle_admin_port() {
  if (s_admin_port) {
    if (s_admin) {
      logging::Logger::set_admin_service(nullptr);
      s_admin->close();
      s_admin->release();
      s_admin = nullptr;
      Log::info("[admin] Admin service stopped on port %d", s_admin_port);
    } else {
      s_admin = new AdminService(nullptr);
      s_admin->retain();
      s_admin->open(s_admin_ip, s_admin_port, s_admin_options);
      logging::Logger::set_admin_service(s_admin);
    }
  }
}

//
// Periodic job base
//

class PeriodicJob {
public:
  void start() { run(); }
  void stop() { m_timer.cancel(); }
protected:
  virtual void run() = 0;
  void next() { m_timer.schedule(5, [this]() { run(); }); }
private:
  Timer m_timer;
};

//
// Periodically clean up pools
//

class PoolCleaner : public PeriodicJob {
  virtual void run() override {
    for (const auto &p : pjs::Pool::all()) {
      p.second->clean();
    }
    WorkerManager::get().recycle();
    next();
  }
};

static PoolCleaner s_pool_cleaner;

//
// Periodically check codebase updates
//

class CodeUpdater : public PeriodicJob {
  virtual void run() override {
    if (!s_has_shutdown) {
      reload_codebase(false);
    }
    next();
  }
};

static CodeUpdater s_code_updater;

//
// Periodically report status
//

class StatusReporter : public PeriodicJob {
public:
  void init(const std::string &address, const Fetch::Options &options) {
    m_url = URL::make(pjs::Value(address).s());
    m_headers = pjs::Object::make();
    m_headers->set("content-type", "application/json");
    m_fetch = new Fetch(m_url->hostname()->str() + ':' + m_url->port()->str(), options);
  }

private:
  virtual void run() override {
    static Data::Producer s_dp("Status Reports");
    if (s_has_shutdown) return;
    if (!m_fetch->busy()) {
      WorkerManager::get().status(
        [this](Status &status) {
          InputContext ic;
          std::stringstream ss;
          status.ip = m_local_ip;
          status.to_json(ss);
          (*m_fetch)(
            Fetch::POST,
            m_url->path(),
            m_headers,
            Data::make(ss.str(), &s_dp),
            [this](http::ResponseHead *head, Data *body) {
              m_local_ip = m_fetch->outbound()->local_address()->str();
            }
          );
        }
      );
    }
    next();
  }

  Fetch *m_fetch = nullptr;
  std::string m_local_ip;
  pjs::Ref<URL> m_url;
  pjs::Ref<pjs::Object> m_headers;
};

static StatusReporter s_status_reporter;

//
// Periodically report metrics
//

class MetricReporter : public PeriodicJob {
  virtual void run() override {
    static Data::Producer s_dp("Metric Reports");
    if (s_has_shutdown) return;
    WorkerManager::get().stats(
      [this](stats::MetricDataSum &metric_data_sum) {
        InputContext ic;
        Data buf;
        Data::Builder db(buf, &s_dp);
        db.push("metrics\n");
        auto conn_id = s_admin_link->connect();
        metric_data_sum.serialize(db, conn_id != m_connection_id);
        db.flush();
        s_admin_link->send(buf);
        m_connection_id = conn_id;
        next();
      }
    );
  }

  int m_connection_id = 0;
};

static MetricReporter s_metric_reporter;

//
// Handle signals
//

class SignalHandler {
public:
  SignalHandler() : m_signals(Net::context()) {
    m_signals.add(SIGINT);
    m_signals.add(SIGHUP);
    m_signals.add(SIGTSTP);
  }

  void start() { wait(); }
  void stop() { m_signals.cancel(); }

private:
  asio::signal_set m_signals;
  bool m_admin_closed = false;
  Timer m_timer;

  void wait() {
    m_signals.async_wait(
      [this](const std::error_code &ec, int sig) {
        InputContext ic;
        if (!ec) handle(sig);
        if (ec != asio::error::operation_aborted) wait();
      }
    );
  }

  void handle(int sig) {
    if (auto worker = Worker::current()) {
      if (worker->handling_signal(sig)) {
        return;
      }
    }

    switch (sig) {
      case SIGINT: {
        if (!m_admin_closed) {
          if (s_admin_link) s_admin_link->close();
          if (s_admin) s_admin->close();
          m_admin_closed = true;
        }

        if (WorkerManager::get().started()) {
          if (s_has_shutdown) {
            Log::info("[shutdown] Forcing to shut down...");
            WorkerManager::get().stop(true);
            drain_events();
          } else {
            Log::info("[shutdown] Shutting down...");
            wait_pipelines();
          }
        } else {
          drain_events();
        }

        s_has_shutdown = true;
        break;
      }

      case SIGHUP:
        reload_codebase(true);
        break;

      case SIGTSTP:
        toggle_admin_port();
        break;
    }
  }

  void wait_pipelines() {
    if (WorkerManager::get().stop()) {
      drain_events();
    } else {
      int n = WorkerManager::get().active_pipeline_count();
      Log::info("[shutdown] Waiting for remaining %d pipelines...", n);
      m_timer.schedule(1, [this]() { wait_pipelines(); });
    }
  }

  void drain_events() {
    Net::current().stop();
    Log::info("[shutdown] Draining event loop...");
    s_pool_cleaner.stop();
    s_code_updater.stop();
    s_status_reporter.stop();
    s_metric_reporter.stop();
    stop();
  }
};

static SignalHandler s_signal_handler;

//
// Program entrance
//

int main(int argc, char *argv[]) {
  int exit_code = 0;

  try {
    MainOptions opts(argc, argv);

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

    Net::init();
    Log::set_filename(opts.log_file);
    Log::set_level(opts.log_level);
    Log::set_topics(opts.log_topics);
    Log::set_local_only(opts.log_local_only);
    Log::init();
    logging::Logger::set_history_length(opts.log_history_limit);
    Listener::set_reuse_port(opts.reuse_port);
    pjs::Math::init();
    crypto::Crypto::init(opts.openssl_engine);
    tls::TLSSession::init();

    s_admin_options.cert = opts.admin_tls_cert;
    s_admin_options.key = opts.admin_tls_key;
    s_admin_options.trusted = opts.admin_tls_trusted;

    std::string admin_ip("::");
    int admin_port = 6060; // default repo port
    auto admin_ip_port = opts.admin_port;
    if (!admin_ip_port.empty()) {
      if (!utils::get_host_port(admin_ip_port, admin_ip, admin_port)) {
        admin_port = std::atoi(admin_ip_port.c_str());
      }
      if (admin_ip.empty()) admin_ip = "::";
    }

    bool is_eval = false;
    bool is_repo = false;
    bool is_repo_proxy = false;
    bool is_remote = false;
    bool is_tls = false;

    if (opts.eval) {
      is_eval = true;

    } else if (opts.filename.empty()) {
      is_repo = true;

    } else if (utils::starts_with(opts.filename, "http://")) {
      is_remote = true;

    } else if (utils::starts_with(opts.filename, "https://")) {
      is_remote = true;
      is_tls = true;

    } else if (utils::is_host_port(opts.filename)) {
      is_repo_proxy = true;

    } else {
      auto full_path = fs::abs_path(opts.filename);
      opts.filename = full_path;
      if (!fs::exists(full_path)) {
        std::string msg("file or directory does not exist: ");
        throw std::runtime_error(msg + full_path);
      }
      is_repo = fs::is_dir(full_path);
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

    if (!is_repo && !opts.init_repo.empty()) {
      throw std::runtime_error("invalid option --init-repo for non-repo mode");
    }

    Store *store = nullptr;
    CodebaseStore *repo = nullptr;
    Codebase *codebase = nullptr;

    std::function<void()> load, fail;
    Timer retry_timer;

    // Start as codebase repo service
    if (is_repo) {
      store = opts.filename.empty()
        ? Store::open_memory()
        : Store::open_level_db(opts.filename);
      repo = new CodebaseStore(store, opts.init_repo);
      s_admin = new AdminService(repo, opts.admin_gui);
      s_admin->retain();
      s_admin->open(admin_ip, admin_port, s_admin_options);
      logging::Logger::set_admin_service(s_admin);

#ifdef PIPY_USE_GUI
      std::cout << std::endl;
      std::cout << "=============================================" << std::endl;
      std::cout << std::endl;
      std::cout << "  You can now view Pipy GUI in the browser:" << std::endl;
      std::cout << std::endl;
      std::cout << "    http://localhost:" << admin_port << '/' << std::endl;
      std::cout << std::endl;
      std::cout << "=============================================" << std::endl;
      std::cout << std::endl;
#endif

    // Start as codebase repo proxy
    } else if (is_repo_proxy) {
      AdminProxy::Options options;
      options.cert = opts.admin_tls_cert;
      options.key = opts.admin_tls_key;
      options.trusted = opts.admin_tls_trusted;
      options.fetch_options.tls = is_tls;
      options.fetch_options.cert = opts.tls_cert;
      options.fetch_options.key = opts.tls_key;
      options.fetch_options.trusted = opts.tls_trusted;
      s_admin_proxy = new AdminProxy(opts.filename, opts.admin_gui);
      s_admin_proxy->open(admin_ip, admin_port, options);

    // Start as a static codebase
    } else {
      if (is_remote) {
        Fetch::Options options;
        options.tls = is_tls;
        options.cert = opts.tls_cert;
        options.key = opts.tls_key;
        options.trusted = opts.tls_trusted;
        codebase = Codebase::from_http(opts.filename, options);
        s_status_reporter.init(opts.filename, options);
      } else if (is_eval) {
        codebase = Codebase::from_fs(
          fs::abs_path("."),
          opts.filename
        );
      } else {
        codebase = Codebase::from_fs(opts.filename);
      }

      codebase->set_current();

      load = [&]() {
        codebase->sync(
          true, [&](bool ok) {
            if (!ok) {
              fail();
              return;
            }

            WorkerManager::get().enable_graph(!opts.no_graph);

            if (!is_repo && !is_remote) {
              WorkerManager::get().on_done(
                [&]() {
                  exit_code = 0;
                  s_pool_cleaner.stop();
                  s_code_updater.stop();
                  s_signal_handler.stop();
                }
              );
            }

            if (!WorkerManager::get().start(opts.threads, opts.force_start)) {
              fail();
              return;
            }

            s_admin_ip = admin_ip;
            s_admin_port = admin_port;

            if (!opts.admin_port.empty() && !opts.admin_port_off) {
              toggle_admin_port();
            }

            s_code_updater.start();

            if (is_remote) {
              AdminLink::TLSSettings tls_settings;
              tls_settings.cert = opts.tls_cert;
              tls_settings.key = opts.tls_key;
              tls_settings.trusted = opts.tls_trusted;
              start_admin_link(opts.filename, is_tls ? &tls_settings : nullptr);
              if (!opts.no_status) s_status_reporter.start();
              if (!opts.no_metrics) s_metric_reporter.start();
            }

            Pipy::on_exit(
              [&](int code) {
                exit_code = code;
                Net::current().stop();
              }
            );
          }
        );
      };

      fail = [&]() {
        if (is_remote) {
          retry_timer.schedule(5, load);
        } else {
          exit_code = -1;
          Net::main().stop();
        }
      };

      load();
    }

    s_pool_cleaner.start();
    s_signal_handler.start();

    Net::current().run();

    if (s_admin) {
      s_admin->close();
      s_admin->release();
    }

    delete s_admin_link;
    delete s_admin_proxy;
    delete repo;

    if (store) store->close();

    crypto::Crypto::free();
    stats::Metric::local().clear();
    Log::shutdown();
    logging::Logger::close_all();
    Timer::cancel_all();

  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return exit_code;
}
