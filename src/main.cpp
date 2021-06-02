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

#include "api/configuration.hpp"
#include "options.hpp"
#include "gui.hpp"
#include "listener.hpp"
#include "module.hpp"
#include "net.hpp"
#include "task.hpp"
#include "utils.hpp"
#include "worker.hpp"

#include <iostream>
#include <list>
#include <string>
#include <tuple>

#include <openssl/opensslv.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

// All filters
#include "filters/connect.hpp"
#include "filters/decompress-body.hpp"
#include "filters/demux.hpp"
#include "filters/dubbo.hpp"
#include "filters/dummy.hpp"
#include "filters/dump.hpp"
#include "filters/exec.hpp"
#include "filters/fork.hpp"
#include "filters/http.hpp"
#include "filters/link.hpp"
#include "filters/mux.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/print.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/socks4.hpp"
#include "filters/tap.hpp"
#include "filters/use.hpp"
#include "filters/wait.hpp"

using namespace pipy;

//
// List of all filters
//

static std::list<Filter*> s_filters {
  new Connect,
  new DecompressBody,
  new Demux,
  new dubbo::Decoder,
  new http::RequestDecoder,
  new http::ResponseDecoder,
  new Dummy,
  new Dump,
  new dubbo::Encoder,
  new http::RequestEncoder,
  new http::ResponseEncoder,
  new Exec,
  new Fork,
  new Link,
  new Mux,
  new OnStart,
  new OnEvent(Event::Type::Data),
  new OnEvent(Event::Type::MessageStart),
  new OnEvent(Event::Type::MessageEnd),
  new OnEvent(Event::Type::SessionEnd),
  new OnBody,
  new OnMessage,
  new Print,
  new ProxySOCKS4,
  new ReplaceStart,
  new ReplaceEvent(Event::Type::Data),
  new ReplaceEvent(Event::Type::MessageStart),
  new ReplaceEvent(Event::Type::MessageEnd),
  new ReplaceEvent(Event::Type::SessionEnd),
  new ReplaceBody,
  new ReplaceMessage,
  new Tap,
  new Use,
  new Wait,
};

//
// Show version
//

static void show_version() {
  std::cout << "Version     : " << PIPY_VERSION << std::endl;
  std::cout << "Commit      : " << PIPY_COMMIT << std::endl;
  std::cout << "Commit Date : " << PIPY_COMMIT_DATE << std::endl;
  std::cout << "Host        : " << PIPY_HOST << std::endl;
  std::cout << "OpenSSL     : " << OPENSSL_VERSION_TEXT << std::endl;
}

//
// Show list of filters
//

static void list_filters() {
  size_t name_width = 0;
  size_t args_width = 0;
  std::list<std::tuple<std::string, std::string, std::string>> list;
  for (auto u : s_filters) {
    std::string name, args, desc;
    auto help = u->help();
    auto i = help.begin();
    if (i != help.end()) name = *i++;
    if (i != help.end()) desc = *i++;
    auto p = name.find('(');
    if (p != std::string::npos) {
      args = name.substr(p);
      name = name.substr(0, p);
    }
    name_width = std::max(name_width, name.length());
    args_width = std::max(args_width, args.length());
    list.push_back({ name, args, desc });
  }
  for (const auto &t : list) {
    const auto &name = std::get<0>(t);
    const auto &args = std::get<1>(t);
    const auto &desc = std::get<2>(t);
    std::cout << name;
    std::cout << std::string(name_width - name.length() + 1, ' ');
    std::cout << args;
    if (desc.length() > 0) {
      std::cout << std::string(args_width - args.length() + 3, ' ');
      std::cout << desc;
    }
    std::cout << std::endl;
  }
}

//
// Show help info of filters
//

static void help_filters() {
  for (auto p : s_filters) {
    std::string name, desc;
    auto help = p->help();
    auto i = help.begin();
    if (i != help.end()) name = *i++;
    if (i != help.end()) desc = *i++;
    if (!name.empty()) {
      std::cout << name << std::endl;
      std::cout << std::string(name.length(), '=') << std::endl;
      std::cout << std::endl;
      std::cout << "  " << desc << std::endl;
      std::cout << std::endl;
      if (i != help.end()) {
        std::list<std::pair<std::string, std::string>> lines;
        for (; i != help.end(); ++i) {
          auto n = i->find('=');
          if (n == std::string::npos) {
            lines.push_back({ utils::trim(*i), "" });
          } else {
            lines.push_back({
              utils::trim(i->substr(0,n)),
              utils::trim(i->substr(n+1)),
            });
          }
        }
        size_t max_width = 0;
        for (const auto &p : lines) max_width = std::max(max_width, p.first.length());
        for (const auto &p : lines) {
          std::cout << "  " << p.first;
          std::cout << std::string(max_width - p.first.length(), ' ');
          std::cout << " - " << p.second << std::endl;
        }
        std::cout << std::endl << std::endl;
      }
    }
  }
}

//
// Show current status
//

template<class T>
void print_table(const T &header, const std::list<T> &rows) {
  int n = header.size();
  int max_width[header.size()];

  for (int i = 0; i < n; i++) {
    max_width[i] = header[i].length();
  }

  for (const auto &row : rows) {
    for (int i = 0; i < n; i++) {
      if (row[i].length() > max_width[i]) {
        max_width[i] = row[i].length();
      }
    }
  }

  int total_width = 0;
  for (int i = 0; i < n; i++) {
    std::string padding(max_width[i] - header[i].length(), ' ');
    std::cout << header[i] << padding << "  ";
    total_width += max_width[i] + 2;
  }

  std::cout << std::endl;
  std::cout << std::string(total_width, '-');
  std::cout << std::endl;

  for (const auto &row : rows) {
    for (int i = 0; i < n; i++) {
      std::string padding(max_width[i] - row[i].length(), ' ');
      std::cout << row[i] << padding << "  ";
    }
    std::cout << std::endl;
  }
}

static void show_status() {
  std::string indentation("  ");
  std::list<std::array<std::string, 3>> pipelines;
  std::list<std::array<std::string, 2>> objects;

  int total_allocated = 0;
  int total_active = 0;
  int total_instances = 0;

  auto current_worker = Worker::current();

  std::multimap<std::string, Pipeline*> stale_pipelines;
  std::multimap<std::string, Pipeline*> current_pipelines;

  Pipeline::for_each([&](Pipeline *p) {
    auto mod = p->module();
    if (!mod) {
      std::string name("[");
      name += p->name();
      name += ']';
      current_pipelines.insert({ name, p });
    } else {
      std::string name(mod->path());
      name += " [";
      name += p->name();
      name += ']';
      if (mod->worker() == current_worker) {
        current_pipelines.insert({ name, p });
      } else if (p->active() > 0) {
        name = std::string("[STALE] ") + name;
        stale_pipelines.insert({ name, p });
      }
    }
  });

  for (const auto &i : current_pipelines) {
    auto p = i.second;
    pipelines.push_back({
      indentation + i.first,
      std::to_string(p->allocated()),
      std::to_string(p->active()),
    });
    total_allocated += p->allocated();
    total_active += p->active();
  }

  for (const auto &i : stale_pipelines) {
    auto p = i.second;
    pipelines.push_back({
      indentation + i.first,
      std::to_string(p->allocated()),
      std::to_string(p->active()),
    });
    total_allocated += p->allocated();
    total_active += p->active();
  }

  pipelines.push_back({
    "TOTAL",
    std::to_string(total_allocated),
    std::to_string(total_active),
  });

  for (const auto &i : pjs::Class::all()) {
    if (auto n = i.second->object_count()) {
      objects.push_back({ indentation + i.first, std::to_string(n) });
      total_instances += n;
    }
  }

  objects.push_back({ "TOTAL", std::to_string(total_instances) });

  std::cout << std::endl;
  print_table({ "PIPELINE", "#ALLOCATED", "#ACTIVE" }, pipelines);
  std::cout << std::endl;
  print_table({ "CLASS", "#INSTANCES" }, objects );
  std::cout << std::endl;
}

//
// Handle SIGTSTP
//

static bool s_need_dump = false;

static void on_sig_tstp(int) {
  Log::info("Received SIGTSTP, dumping...");
  s_need_dump = true;
}

//
// Handle SIGHUP
//

static bool s_need_reload = false;

static void on_sig_hup(int) {
  Log::info("Received SIGHUP, reloading script...");
  s_need_reload = true;
}

//
// Handle SIGINT
//

static bool s_need_shutdown = false;
static bool s_force_shutdown = false;

static void on_sig_int(int) {
  if (s_need_shutdown) {
    Log::info("Forcing to shut down...");
    s_force_shutdown = true;
  } else {
    Log::info("Received SIGINT, shutting down...");
    s_need_shutdown = true;
  }
}

//
// Periodically check signals
//

static void start_checking_signals() {
  static Timer timer;
  static std::function<void()> poll;
  poll = [&]() {
    if (s_need_dump) {
      show_status();
      s_need_dump = false;
    }
    if (s_force_shutdown) {
      Net::stop();
      Log::info("Stopped.");
    } else if (s_need_shutdown) {
      Listener::close_all();
      Task::stop_all();
      int n = 0;
      Pipeline::for_each(
        [&](Pipeline *pipeline) {
          n += pipeline->active();
        }
      );
      if (n > 0) {
        Log::info("Waiting for remaining %d sessions... Press Ctrl-C again to force shutdown", n);
      } else {
        Net::stop();
        Log::info("Stopped.");
      }
    } else if (s_need_reload) {
      s_need_reload = false;
      auto current_worker = Worker::current();
      if (!current_worker) {
        Log::error("No script running");
      } else {
        auto root_path = current_worker->root_path();
        auto root_name = current_worker->root()->path();
        auto worker = Worker::make(root_path);
        if (worker->load_module(root_name) && worker->start()) {
          current_worker->unload();
          Log::info("Script reloaded: %s", root_name.c_str());
        } else {
          worker->unload();
          Log::error("Failed reloading script: %s", root_name.c_str());
        }
      }
    }
    timer.schedule(1, poll);
  };
  poll();
}

//
// Program entrance
//

int main(int argc, char *argv[]) {
  try {
    Options opts(argc, argv);

    if (opts.version) {
      show_version();
      return 0;
    }

    if (opts.help) {
      Options::show_help();
      return 0;
    }

    if (opts.help_filters) {
      std::cout << std::endl;
      help_filters();
      std::cout << std::endl;
      return 0;
    }

    if (opts.list_filters) {
      std::cout << std::endl;
      list_filters();
      std::cout << std::endl;
      return 0;
    }

    Log::set_level(opts.log_level);

    Configuration::set_reuse_port(opts.reuse_port);

    char full_path[PATH_MAX];
    realpath(opts.filename.c_str(), full_path);

    struct stat st;
    if (stat(full_path, &st)) {
      std::string msg("file or directory does not exist: ");
      throw std::runtime_error(msg + full_path);
    }

    std::string root_path, root_name;
    if (S_ISDIR(st.st_mode)) {
      if (!opts.gui_port) {
        throw std::runtime_error("script file not specified");
      }
      root_path = full_path;
    } else {
      std::string script_path(full_path);
      auto i = script_path.find_last_of("/\\");
      root_path = script_path.substr(0, i);
      root_name = script_path.substr(i);
    }

    chdir(root_path.c_str());

    signal(SIGTSTP, on_sig_tstp);
    signal(SIGHUP, on_sig_hup);
    signal(SIGINT, on_sig_int);

    Gui gui(root_path);
    if (opts.gui_port) {
      gui.open(opts.gui_port);
      opts.verify = false;
    }

    if (!root_name.empty()) {
      auto worker = Worker::make(root_path);
      auto mod = worker->load_module(root_name);
      if (!mod) return -1;
      if (!opts.verify) if (!worker->start()) return -1;
    }

    if (!opts.verify) {
      start_checking_signals();
      Net::run();
    }

    std::cout << "Done." << std::endl;

  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
