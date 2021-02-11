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

#include "config.hpp"
#include "options.hpp"
#include "pipeline.hpp"
#include "listener.hpp"
#include "js.hpp"
#include "utils.hpp"

#include <iostream>
#include <signal.h>

// all modules
#include "modules/clone.hpp"
#include "modules/counter.hpp"
#include "modules/dubbo.hpp"
#include "modules/dummy.hpp"
#include "modules/erase.hpp"
#include "modules/filter.hpp"
#include "modules/hello.hpp"
#include "modules/hessian2.hpp"
#include "modules/http.hpp"
#include "modules/insert.hpp"
#include "modules/json.hpp"
#include "modules/message.hpp"
#include "modules/print.hpp"
#include "modules/prometheus.hpp"
#include "modules/proxy.hpp"
#include "modules/queue.hpp"
#include "modules/script.hpp"
#include "modules/serve-static.hpp"
#include "modules/tap.hpp"
#include "modules/timestamp.hpp"
#include "modules/xml.hpp"

using namespace pipy;

static Config s_config;
static std::list<Listener*> s_listeners;
static std::string s_config_base_path;
static std::string s_config_file_path;
static uint64_t s_config_file_time = 0;
static bool s_config_watch = false;

//
// module registry
//

static std::map<std::string, Module*> s_module_registry {
  { "clone"                 , new Clone },
  { "count"                 , new Counter },
  { "decode-dubbo"          , new dubbo::Decoder },
  { "decode-hessian2"       , new hessian2::Decoder },
  { "decode-http-request"   , new http::RequestDecoder },
  { "decode-http-response"  , new http::ResponseDecoder },
  { "decode-json"           , new json::Decoder },
  { "decode-xml"            , new xml::Decoder },
  { "dummy"                 , new Dummy },
  { "encode-dubbo"          , new dubbo::Encoder },
  { "encode-hessian2"       , new hessian2::Encoder },
  { "encode-http-request"   , new http::RequestEncoder },
  { "encode-http-response"  , new http::ResponseEncoder },
  { "encode-json"           , new json::Encoder },
  { "encode-xml"            , new xml::Encoder },
  { "erase"                 , new Erase },
  { "filter"                , new Filter },
  { "hello"                 , new Hello },
  { "insert"                , new Insert(false) },
  { "message"               , new Message },
  { "print"                 , new Print },
  { "prometheus"            , new Prometheus },
  { "proxy"                 , new Proxy },
  { "proxy-tcp"             , new ProxyTCP },
  { "replace"               , new Insert(true) },
  { "queue"                 , new Queue },
  { "script"                , new Script },
  { "serve-static"          , new ServeStatic },
  { "tap"                   , new Tap },
  { "timestamp"             , new Timestamp },
};

//
// show version
//

static void show_version() {
  std::cout << "Version    : " << PIPED_VERSION << std::endl;
  std::cout << "Commit     : " << PIPED_COMMIT << std::endl;
  std::cout << "Commit Date: " << PIPED_COMMIT_DATE << std::endl;
}

//
// show list of modules
//

static void list_modules() {
  size_t max_width = 0;
  for (const auto &p : s_module_registry) max_width = std::max(max_width, p.first.length());
  for (const auto &p : s_module_registry) {
    auto name = p.first;
    auto help = p.second->help();
    if (help.size() > 0) {
      auto padding = std::string(max_width - name.length() + 3, ' ');
      std::cout << name << padding << help.front() << std::endl;
    } else {
      std::cout << name << std::endl;
    }
  }
}

//
// show help info of modules
//

static void help_modules() {
  for (const auto &p : s_module_registry) {
    auto name = p.first;
    auto help = p.second->help();
    if (help.size() > 0) {
      std::cout << name << std::endl;
      std::cout << std::string(name.length(), '=') << std::endl;
      std::cout << std::endl;
      std::cout << "  " << help.front() << std::endl;
      std::cout << std::endl;
      if (help.size() > 1) {
        std::list<std::pair<std::string, std::string>> lines;
        for (auto p = ++help.begin(); p != help.end(); ++p) {
          auto i = p->find('=');
          if (i == std::string::npos) {
            lines.push_back({ utils::trim(*p), "" });
          } else {
            lines.push_back({
              utils::trim(p->substr(0,i)),
              utils::trim(p->substr(i+1)),
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
        std::cout << std::endl;
      }
    }
  }
}

//
// create a module chain
//

static bool create_module_chain(const Config::Pipeline &pipeline, std::list<std::unique_ptr<Module>> &chain) {
  for (auto &module : pipeline.modules) {
    auto p = s_module_registry.find(module.name);
    if (p == s_module_registry.end()) {
      std::cerr << "Unknown module name '" << module.name << "' at line " << module.line << std::endl;
      return false;
    }
    auto *module_instance = p->second->clone();
    try {
      module_instance->config(module.params);
    } catch (std::runtime_error &e) {
      std::cerr << "Error when configuring module at line " << module.line << ": ";
      std::cerr << e.what() << std::endl;
      return false;
    }
    chain.push_back(std::unique_ptr<Module>(module_instance));
  }
  return true;
}

//
// reload configuration
//

static void reload_config() {
  if (s_config_file_path.empty()) {
    Log::error("No configuration file to reload");
    return;
  }

  Config config;
  Log::info("Reloading configuration from file %s", s_config_file_path.c_str());
  if (!config.parse_file(s_config_file_path)) {
    return;
  }

  if (config.pipelines.size() > s_config.pipelines.size()) {
    Log::error("New configuration has more pipelines than the current");
    return;
  }

  if (config.pipelines.size() < s_config.pipelines.size()) {
    Log::error("New configuration has less pipelines than the current");
    return;
  }

  std::map<std::string, std::list<std::unique_ptr<Module>>> chains;

  for (const auto &pipeline : config.pipelines) {
    auto found = false;
    for (const auto &p : s_config.pipelines) {
      if (p.name == pipeline.name) {
        found = true;
        break;
      }
    }
    if (!found) {
      Log::error("Pipeline not found: %s", pipeline.name.c_str());
      return;
    }
    auto &chain = chains[pipeline.name];
    if (!create_module_chain(pipeline, chain)) return;
  }

  for (auto &p : chains) {
    auto pipeline = Pipeline::get(p.first);
    if (!pipeline) {
      Log::error("Pipeline not found: %s", p.first.c_str());
      continue;
    }
    pipeline->update(p.second);
  }
}

//
// handles SIGHUP
//

static bool s_need_reload_config = false;

static void on_sig_hup(int) {
  Log::error("Received SIGHUP, reloading configuration...");
  s_need_reload_config = true;
}

//
// handles SIGINT
//

static bool s_need_shut_down = false;

static void on_sig_int(int) {
  Log::error("Received SIGINT, shutting down...");
  s_need_shut_down = true;
}

//
// main
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

    if (opts.help_modules) {
      std::cout << std::endl;
      help_modules();
      std::cout << std::endl;
      return 0;
    }

    if (opts.list_modules) {
      std::cout << std::endl;
      list_modules();
      std::cout << std::endl;
      return 0;
    }

    Log::set_level(opts.log_level);
    s_config_watch = opts.watch_config_file;

    if (opts.config_filename.empty()) {
      std::cerr << "missing config file" << std::endl;
      return -1;
    }

    char config_path[1000];
    realpath(opts.config_filename.c_str(), config_path);

    std::cout << "Loading configuration from file " << config_path << std::endl;
    if (!s_config.parse_file(config_path)) return -1;
    s_config_file_time = utils::get_file_time(s_config_file_path);
    s_config_file_path = config_path;
    s_config_base_path = std::string(config_path, s_config_file_path.find_last_of("/\\"));

    if (opts.verify) {
      std::cout << "Configuration verified" << std::endl;
      return 0;
    }

    Listener::set_reuse_port(opts.reuse_port);

  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  ServeStatic::set_root_path(s_config_base_path);

  auto js_worker = js::Worker::current();
  js_worker->set_root_path(s_config_base_path.c_str());

  for (const auto &pipeline : s_config.pipelines) {
    std::list<std::unique_ptr<Module>> chain;
    if (!create_module_chain(pipeline, chain)) return -1;
    auto p = new Pipeline(chain);
    Pipeline::add(pipeline.name, p);
    std::string ip;
    int port;
    if (utils::get_ip_port(pipeline.name, ip, port)) {
      auto listener = Listener::listen(ip, port, p);
      s_listeners.push_back(listener);
    }
  }

  signal(SIGHUP, on_sig_hup);
  signal(SIGINT, on_sig_int);

  std::function<void()> check_signals;
  check_signals = [&]() {
    if (s_need_shut_down) {
      if (s_listeners.empty()) {
        if (Pipeline::session_total() > 0) {
          Log::error("Waiting for remaining %d sessions...", Pipeline::session_total());
        } else {
          Listener::stop();
          Log::info("Stopped.");
        }
      } else {
        while (!s_listeners.empty()) {
          auto listener = s_listeners.back();
          Log::info("Shutdown %s:%i", listener->ip().c_str(), listener->port());
          s_listeners.back()->close();
          s_listeners.pop_back();
        }
      }
    } else {
      auto config_updated = false;
      if (s_need_reload_config) {
        s_need_reload_config = false;
        config_updated = true;
        Log::info("About to reload configuration due to SIGHUP");
      } else if (!config_updated) {
        if (s_config_watch) {
          auto t = utils::get_file_time(s_config_file_path);
          if (t > s_config_file_time) {
            config_updated = true;
            s_config_file_time = t;
            Log::info("About to reload configuration due to timestamp change");
          }
        }
      }
      if (config_updated) {
        reload_config();
      }
    }
    Listener::set_timeout(1, check_signals);
  };

  check_signals();

  Listener::run();

  return 0;
}
