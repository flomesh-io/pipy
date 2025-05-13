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
// Handle signals
//

class SignalHandler {
public:
  SignalHandler(WorkerThread *worker_thread)
    : m_worker_thread(worker_thread)
  {
    bool started = false;
    std::condition_variable start_cv;
    std::mutex start_cv_mutex;
    std::unique_lock<std::mutex> lock(start_cv_mutex);

    m_thread = std::thread(
      [&]() {
        m_net = &Net::current();
        {
          std::lock_guard<std::mutex> lock(start_cv_mutex);
          started = true;
          start_cv.notify_one();
        }
        asio::signal_set signals(Net::context());
        signals.add(SIGNAL_STOP);
        signals.add(SIGNAL_RELOAD);
        signals.add(SIGNAL_ADMIN);
        std::function<void()> wait;
        wait = [&]() {
          signals.async_wait(
            [&](const std::error_code &ec, int sig) {
              if (ec != asio::error::operation_aborted) {
                if (!ec) {
                  m_worker_thread->signal(sig);
                }
                wait();
              }
            }
          );
        };
        wait();
        m_net->run();
      }
    );

    start_cv.wait(lock, [&]() { return started; });
  }

  ~SignalHandler() {
    m_net->stop();
    m_thread.join();
  }

private:
  std::thread m_thread;
  WorkerThread* m_worker_thread;
  Net* m_net;
};

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
      throw std::string("TODO");

    // Start as codebase repo proxy
    } else if (is_repo_proxy) {
      throw std::string("TODO");

    // Start using a builtin codebase
    } else if (is_builtin) {
      auto name = opts.filename.substr(6);
      store = Store::open_memory();
      repo = new CodebaseStore(store);
      codebase = Codebase::from_store(repo, name);

    // Start using a remote codebase
    } else if (is_remote) {
      Fetch::Options options;
      options.tls = is_tls;
      options.cert = opts.tls_cert;
      options.key = opts.tls_key;
      options.trusted = opts.tls_trusted;
      codebase = Codebase::from_http(opts.filename, options);
      throw std::string("TODO");

    // Start using a local codebase
    } else if (is_file_found) {
      codebase = Codebase::from_fs(opts.filename);

    // Start using a one-liner
    } else {
      codebase = Codebase::from_fs(fs::abs_path("."), opts.filename);
    }

    codebase = Codebase::from_root(codebase);

    WorkerThread t(codebase);
    SignalHandler sh(&t);
    t.main(opts.arguments);

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
