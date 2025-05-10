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
#include "filters/tls.hpp"
#include "input.hpp"
#include "listener.hpp"
#include "main-options.hpp"
#include "net.hpp"
#include "os-platform.hpp"
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

static bool s_has_shutdown = false;

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

#ifdef PIPY_USE_GUI
  std::cout << "Builtin GUI      : " << "Yes" << std::endl;
#else
  std::cout << "Builtin GUI      : " << "No" << std::endl;
#endif

#ifdef PIPY_USE_CODEBASES
  std::cout << "Builtin Codebases: " << "Yes" << std::endl;
#else
  std::cout << "Builtin Codebases: " << "No" << std::endl;
#endif

#ifdef PIPY_DEFAULT_OPTIONS
  std::cout << "Default Options  : " << PIPY_DEFAULT_OPTIONS << std::endl;
#endif
}

//
// Reload codebase
//

static void reload_codebase(bool force, const std::function<void()> &cb = nullptr) {
  if (auto *codebase = Codebase::current()) {
    Log::debug(Log::CODEBASE, "[codebase] Start syncing codebase");
    codebase->sync(
      force, [=](bool ok) {
        Log::debug(Log::CODEBASE, "[codebase] Codebase synced (updated = %d)", ok);
        if (ok) WorkerManager::get().reload();
        if (cb) cb();
      }
    );
  }
}

//
// Periodic job base
//

class PeriodicJob {
public:
  void start() { run(); }
  void stop() { if (m_timer) m_timer->cancel(); }
protected:
  virtual void run() = 0;
  void next() {
    if (!m_timer) m_timer = std::unique_ptr<Timer>(new Timer);
    m_timer->schedule(5, [this]() { run(); });
  }

private:
  std::unique_ptr<Timer> m_timer;
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
      reload_codebase(false, [=]() { next(); });
    }
  }
};

static CodeUpdater s_code_updater;

//
// Handle signals
//

class SignalHandler {
public:
  SignalHandler() : m_signals(Net::context()) {
    m_signals.add(SIGNAL_STOP);
    m_signals.add(SIGNAL_RELOAD);
    m_signals.add(SIGNAL_ADMIN);
  }

  void start() { wait(); }
  void stop() { m_signals.cancel(); }

private:
  asio::signal_set m_signals;
  std::unique_ptr<Timer> m_timer;

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
    switch (sig) {
      case SIGNAL_STOP: {
        exit_process(false);
        break;
      }
      case SIGNAL_RELOAD:
        reload_codebase(true);
        break;
      case SIGNAL_ADMIN:
        // toggle_admin_port(); // TODO
        break;
    }
  }

  void wait_workers() {
    if (WorkerManager::get().stop()) {
      stop_all();
    } else {
      Log::info("[shutdown] Waiting for workers to drain...");
      if (!m_timer) m_timer = std::unique_ptr<Timer>(new Timer);
      m_timer->schedule(1, [this]() { wait_workers(); });
    }
  }

  void stop_all() {
    Net::current().stop();
    s_pool_cleaner.stop();
    s_code_updater.stop();
    stop();
  }

public:
  void exit_process(bool force) {
    if (WorkerManager::get().started()) {
      if (force || s_has_shutdown) {
        Log::info("[shutdown] Forcing to shut down...");
        WorkerManager::get().stop(true);
        stop_all();
      } else {
        Log::info("[shutdown] Shutting down...");
        wait_workers();
      }
    } else {
      stop_all();
    }

    s_has_shutdown = true;
  }
};

static SignalHandler s_signal_handler;

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

    Store *store = nullptr;
    CodebaseStore *repo = nullptr;
    Codebase *codebase = nullptr;

    std::function<void()> load, exit, fail;
    Timer retry_timer;

    // Start as codebase repo service
    if (is_repo) {
      store = opts.filename.empty()
        ? Store::open_memory()
        : Store::open_memory(); // TODO: Sqlite store
      repo = new CodebaseStore(store, opts.init_repo);

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

      throw std::string("TODO");

    // Start as codebase repo proxy
    } else if (is_repo_proxy) {

      throw std::string("TODO");

    // Start as a static codebase
    } else {
      if (is_builtin) {
        auto name = opts.filename.substr(6);
        store = Store::open_memory();
        repo = new CodebaseStore(store);
        codebase = Codebase::from_store(repo, name);
      } else if (is_remote) {
        Fetch::Options options;
        options.tls = is_tls;
        options.cert = opts.tls_cert;
        options.key = opts.tls_key;
        options.trusted = opts.tls_trusted;
        codebase = Codebase::from_http(opts.filename, options);
        throw std::string("TODO");
      } else if (is_file_found) {
        codebase = Codebase::from_fs(opts.filename);
      } else {
        codebase = Codebase::from_fs(
          fs::abs_path("."),
          opts.filename
        );
      }

      codebase = Codebase::from_root(codebase);
      codebase->set_current();

      bool started = false, start_error = false;

      load = [&]() {
        codebase->sync(
          true, [&](bool ok) {
            if (!ok) {
              fail();
              return;
            }

            auto &wm = WorkerManager::get();
            wm.argv(opts.arguments);
            wm.enable_graph(!opts.no_graph);

            if ((is_repo || is_remote) && !opts.no_reload) {
              wm.on_ended(exit);
            } else {
              wm.on_done(exit);
            }

            try {
              started = wm.start(opts.threads, opts.force_start);
            } catch (std::runtime_error &) {
              start_error = true;
            }

            if (!started) {
              fail();
              return;
            }

            if (!opts.no_reload) {
              s_code_updater.start();
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

      exit = [&]() {
        if (!is_remote || opts.no_reload) {
          s_pool_cleaner.stop();
          s_code_updater.stop();
          s_signal_handler.stop();
        }
        exit_code = 0;
      };

      fail = [&]() {
        if (is_remote && !opts.no_reload) {
          retry_timer.schedule(5, load);
        } else {
          if (start_error) {
            if (is_file && !is_file_found) {
              std::cerr << "file or directory does not exist either with the input string as a pathname" << std::endl;
            }
            exit_code = -1;
          } else {
            exit_code = 0;
          }
          Net::main().stop();
        }
      };

      load();
    }

    s_pool_cleaner.start();
    s_signal_handler.start();

    Net::current().run();

    delete repo;

    if (store) store->close();

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
  s_signal_handler.exit_process(force);
}

#else // !PIPY_SHARED

int main(int argc, char *argv[]) {
  return pipy_main(argc, argv);
}

#endif // PIPY_SHARED
