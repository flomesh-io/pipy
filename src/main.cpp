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
#include "codebase.hpp"
#include "gui.hpp"
#include "listener.hpp"
#include "module.hpp"
#include "net.hpp"
#include "options.hpp"
#include "outbound.hpp"
#include "task.hpp"
#include "utils.hpp"
#include "worker.hpp"

#include <limits.h>
#include <signal.h>
#include <stdlib.h>

#include <list>
#include <string>
#include <tuple>

#include <openssl/opensslv.h>
#include <openssl/ssl.h>

// All filters
#include "filters/connect.hpp"
#include "filters/decompress-message.hpp"
#include "filters/demux.hpp"
#include "filters/dubbo.hpp"
#include "filters/dummy.hpp"
#include "filters/dump.hpp"
#include "filters/exec.hpp"
#include "filters/fork.hpp"
#include "filters/http.hpp"
#include "filters/link.hpp"
#include "filters/merge.hpp"
#include "filters/mux.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/pack.hpp"
#include "filters/print.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/socks.hpp"
#include "filters/socks4.hpp"
#include "filters/split.hpp"
#include "filters/tap.hpp"
#include "filters/tls.hpp"
#include "filters/use.hpp"
#include "filters/wait.hpp"

using namespace pipy;

//
// List of all filters
//

static std::list<Filter*> s_filters {
  new tls::Server,
  new Connect,
  new tls::Client,
  new DecompressHTTP,
  new DecompressMessage,
  new Demux,
  new http::Demux,
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
  new Merge,
  new Mux,
  new http::Mux,
  new OnStart,
  new OnEvent(Event::Type::Data),
  new OnEvent(Event::Type::MessageStart),
  new OnEvent(Event::Type::MessageEnd),
  new OnEvent(Event::Type::SessionEnd),
  new OnBody,
  new OnMessage,
  new Pack,
  new Print,
  new ProxySOCKS,
  new ProxySOCKS4,
  new ReplaceStart,
  new ReplaceEvent(Event::Type::Data),
  new ReplaceEvent(Event::Type::MessageStart),
  new ReplaceEvent(Event::Type::MessageEnd),
  new ReplaceEvent(Event::Type::SessionEnd),
  new ReplaceBody,
  new ReplaceMessage,
  new Split,
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
  std::list<std::array<std::string, 2>> objects;
  std::list<std::array<std::string, 3>> chunks;
  std::list<std::array<std::string, 3>> pipelines;
  std::list<std::array<std::string, 3>> inbounds;
  std::list<std::array<std::string, 6>> outbounds;

  int total_allocated = 0;
  int total_active = 0;
  int total_chunks = 0;
  int total_instances = 0;
  int total_inbound_connections = 0;
  int total_inbound_buffered = 0;

  auto current_worker = Worker::current();

  for (const auto &i : pjs::Class::all()) {
    if (auto n = i.second->object_count()) {
      objects.push_back({ indentation + i.first, std::to_string(n) });
      total_instances += n;
    }
  }

  objects.push_back({ "TOTAL", std::to_string(total_instances) });

  Data::Producer::for_each([&](Data::Producer *producer) {
    chunks.push_back({
      producer->name(),
      std::to_string(producer->current() * DATA_CHUNK_SIZE / 1024),
      std::to_string(producer->peak() * DATA_CHUNK_SIZE / 1024),
    });
    total_chunks += producer->current();
  });

  chunks.push_back({ "TOTAL", std::to_string(total_chunks * DATA_CHUNK_SIZE / 1024), "n/a" });

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

  Listener::for_each([&](Listener *listener) {
    int count = 0;
    int buffered = 0;
    listener->for_each_inbound([&](Inbound *inbound) {
      count++;
      buffered += inbound->buffered();
    });
    char count_peak[100];
    sprintf(count_peak, "%d/%d", count, listener->peak_connections());
    inbounds.push_back({
      std::to_string(listener->port()),
      std::string(count_peak),
      std::to_string(buffered/1024),
    });
    total_inbound_connections += count;
    total_inbound_buffered += buffered;
  });

  inbounds.push_back({
    "TOTAL",
    std::to_string(total_inbound_connections),
    std::to_string(total_inbound_buffered),
  });

  struct OutboundSum {
    int connections = 0;
    int buffered = 0;
    int overflowed = 0;
    double max_connection_time = 0;
    double avg_connection_time = 0;
  };

  std::map<std::string, OutboundSum> outbound_sums;
  Outbound::for_each([&](Outbound *outbound) {
    char key[1000];
    std::sprintf(key, "%s:%d", outbound->host().c_str(), outbound->port());
    auto conn_time = outbound->connection_time() / (outbound->retries() + 1);
    auto &sum = outbound_sums[key];
    sum.connections++;
    sum.buffered += outbound->buffered();
    sum.overflowed += outbound->overflowed();
    sum.max_connection_time = std::max(sum.max_connection_time, conn_time);
    sum.avg_connection_time += conn_time;
  });

  for (const auto &p : outbound_sums) {
    const auto &sum = p.second;
    outbounds.push_back({
      p.first,
      std::to_string(sum.connections),
      std::to_string(sum.buffered/1024),
      std::to_string(sum.overflowed),
      std::to_string(int(sum.max_connection_time)),
      std::to_string(int(sum.avg_connection_time / sum.connections)),
    });
  }

  std::cout << std::endl;
  print_table({ "CLASS", "#INSTANCES" }, objects );
  std::cout << std::endl;
  print_table({ "DATA", "CURRENT(KB)", "PEAK(KB)" }, chunks );
  std::cout << std::endl;
  print_table({ "PIPELINE", "#ALLOCATED", "#ACTIVE" }, pipelines);
  std::cout << std::endl;
  print_table({ "INBOUND", "#CONNECTIONS", "BUFFERED(KB)" }, inbounds);
  std::cout << std::endl;
  print_table({ "OUTBOUND", "#CONNECTIONS", "BUFFERED(KB)", "#OVERFLOWED", "MAX_CONN_TIME", "AVG_CONN_TIME" }, outbounds);
  std::cout << std::endl;
}

//
// Handle SIGTSTP
//

static bool s_need_dump = false;

static void on_sig_tstp(int) {
  s_need_dump = true;
}

//
// Handle SIGHUP
//

static bool s_need_reload = false;

static void on_sig_hup(int) {
  s_need_reload = true;
}

void main_trigger_reload() {
  s_need_reload = true;
}

//
// Handle SIGINT
//

static bool s_need_shutdown = false;
static bool s_force_shutdown = false;

static void on_sig_int(int) {
  if (s_need_shutdown) {
    s_force_shutdown = true;
  } else {
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
      Log::info("Received SIGTSTP, dumping...");
      show_status();
      s_need_dump = false;
    }
    if (s_force_shutdown) {
      Log::info("Forcing to shut down...");
      Net::stop();
      Log::info("Stopped.");
    } else if (s_need_shutdown) {
      Log::info("Received SIGINT, shutting down...");
      Listener::close_all();
      if (auto worker = Worker::current()) worker->stop();
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
        auto codebase = Codebase::current();
        codebase->update([](bool ok) {
          if (!ok) {
            Log::error("Failed updating script");
            return;
          }
          auto codebase = Codebase::current();
          auto &entry = codebase->entry();
          if (entry.empty()) {
            Log::error("Script has no entry point");
            return;
          }
          auto current_worker = Worker::current();
          auto worker = Worker::make();
          if (worker->load_module(entry) && worker->start()) {
            current_worker->stop();
            Log::info("Script reloaded");
          } else {
            worker->stop();
            Log::error("Failed reloading script");
          }
        });
      }
    }
    timer.schedule(0.2, poll);
  };
  poll();
}

//
// Periodically check codebase updates
//

static void start_checking_updates() {
  static Timer timer;
  static std::function<void()> poll;
  poll = [&]() {
    if (!s_need_shutdown) {
      Codebase::current()->check(
        [&](bool updated) {
          if (updated) {
            s_need_reload = true;
          }
        }
      );
    }
    timer.schedule(5, poll);
  };
  poll();
}

//
// Program entrance
//

int main(int argc, char *argv[]) {
  SSL_load_error_strings();
  SSL_library_init();
  OpenSSL_add_all_algorithms();

  tls::TLSSession::init();

  int ret_val = 0;

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

    if (!std::strncmp(opts.filename.c_str(), "http://", 7)) {
      auto codebase = new CodebaseHTTP(opts.filename);
      codebase->set_current();

    } else {
      char full_path[PATH_MAX];
      realpath(opts.filename.c_str(), full_path);
      auto codebase = new CodebaseFS(full_path);
      codebase->set_current();
      chdir(codebase->base().c_str());
    }

    Gui gui;

    Codebase::current()->update(
      [&](bool ok) {
        if (!ok) {
          ret_val = -1;
          return;
        }

        const auto &entry = Codebase::current()->entry();
        if (entry.empty() && !opts.gui_port) {
          std::cerr << "No script file specified" << std::endl;
          ret_val = -1;
          return;
        }

        if (!entry.empty()) {
          auto worker = Worker::make();
          auto mod = worker->load_module(entry);

          if (!mod) {
            ret_val = -1;
            Net::stop();
            return;
          }

          if (opts.verify) {
            Net::stop();
            return;
          }

          if (!worker->start()) {
            ret_val = -1;
            Net::stop();
            return;
          }
        }

        if (opts.gui_port && !opts.verify) {
          gui.open(opts.gui_port);
        }

        signal(SIGTSTP, on_sig_tstp);
        signal(SIGHUP, on_sig_hup);
        signal(SIGINT, on_sig_int);

        start_checking_signals();
        start_checking_updates();
      }
    );

    Net::run();
    std::cout << "Done." << std::endl;

  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return ret_val;
}