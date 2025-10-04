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

#include "main-options.hpp"
#include "fs.hpp"
#include "data.hpp"
#include "utils.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

namespace pipy {

static Data::Producer s_dp("Command Line Options");

void MainOptions::show_help() {
  std::cout << "Usage: pipy [options] [<expression | pathname | URL>]" << std::endl;
  std::cout << std::endl;

  std::cout << "URL can be one of:" << std::endl;
  std::cout << "  - http[s]://<host>:<port>/<codebase> Run <codebase> from the remote repo at <host>:<port>" << std::endl;
  std::cout << "  - http[s]://<host>:<port>            Run as a proxy to the remote repo at <host>:<port>" << std::endl;
  std::cout << "  - repo://<codebase>                  Run a builtin codebase" << std::endl;
  std::cout << std::endl;

#ifdef PIPY_DEFAULT_OPTIONS
  std::cout << "Default options: " << std::endl;
  std::cout << "  " << PIPY_DEFAULT_OPTIONS << std::endl;
  std::cout << "  Start your options with --pipy to cancel all default options" << std::endl;
  std::cout << std::endl;
#endif

  std::cout << "Options:" << std::endl;
  std::cout << "  -h, -help, --help                    Show help information" << std::endl;
  std::cout << "  -v, -version, --version              Show version information" << std::endl;
  std::cout << "  -e, -eval, --eval                    Evaluate the given string as script" << std::endl;
  std::cout << "  -f, -file, --file                    Interpret the given string as a pathname" << std::endl;
  std::cout << "  --, -args, --args                    Indicate the end of Pipy options and the start of script arguments" << std::endl;
  std::cout << "  --pipy-options                       Indicate the beginning of Pipy options while processing script arguments" << std::endl;
  std::cout << "  --threads=<number>                   Number of worker threads (1, 2, ... max)" << std::endl;
  std::cout << "  --log-file=<filename>                Set the pathname of the log file" << std::endl;
  std::cout << "  --log-file-max-size=<size>           Set the maximum log file size in bytes" << std::endl;
  std::cout << "  --log-file-max-count=<number>        Set the number of log files to keep" << std::endl;
  std::cout << "  --log-file-rotate-interval=<time>    Set the time of interval log files are rotated (such as '15m', '1h', ...)" << std::endl;
  std::cout << "  --log-level=<debug|info|warn|error>  Set the level of log output" << std::endl;
  std::cout << "  --log-history-limit=<size>           Set size limit of log history in bytes" << std::endl;
  std::cout << "  --log-local=<stdout|stderr|null>     Select local output for system log" << std::endl;
  std::cout << "  --log-local-only                     Do not send out system log" << std::endl;
  std::cout << "  --no-reload                          Do not check for remote codebase updates" << std::endl;
  std::cout << "  --no-status                          Do not report current status to the repo" << std::endl;
  std::cout << "  --no-metrics                         Do not report metrics to the repo" << std::endl;
  std::cout << "  --trace-objects                      Enable tracing the locations of object construction" << std::endl;
  std::cout << "  --instance-uuid=<uuid>               Specify a UUID for this worker process" << std::endl;
  std::cout << "  --instance-name=<name>               Specify a name for this worker process" << std::endl;
  std::cout << "  --reuse-port                         Enable kernel load balancing for all listening ports" << std::endl;
  std::cout << "  --admin-port=<[[ip]:]port>           Enable administration service on the specified port" << std::endl;
  std::cout << "  --admin-port-off                     Do not start administration service at startup" << std::endl;
  std::cout << "  --admin-tls-cert=<filename>          Administration service certificate" << std::endl;
  std::cout << "  --admin-tls-key=<filename>           Administration service private key" << std::endl;
  std::cout << "  --admin-tls-trusted=<filename>       Client certificate(s) trusted by administration service" << std::endl;
  std::cout << "  --tls-cert=<filename>                Client certificate in communication to administration service" << std::endl;
  std::cout << "  --tls-key=<filename>                 Client private key in communication to administration service" << std::endl;
  std::cout << "  --tls-trusted=<filename>             Administration service certificate(s) trusted by client" << std::endl;
  std::cout << "  --openssl-engine=<id>                Select an OpenSSL engine" << std::endl;
  std::cout << std::endl;
}

static const struct {
  Log::Topic topic;
  const char *name;
} s_topic_names[] = {
  { Log::ALLOC    , "alloc" },
  { Log::THREAD   , "thread" },
  { Log::PIPELINE , "pipeline" },
  { Log::DUMP     , "dump" },
  { Log::LISTENER , "listener" },
  { Log::INBOUND  , "inbound" },
  { Log::OUTBOUND , "outbound" },
  { Log::SOCKET   , "socket" },
  { Log::FILES    , "files" },
  { Log::SUBPROC  , "subproc" },
  { Log::TCP      , "tcp" },
  { Log::UDP      , "udp" },
  { Log::HTTP2    , "http2" },
  { Log::ELF      , "elf" },
  { Log::BPF      , "bpf" },
  { Log::USER     , "user" },
  { Log::CODEBASE , "codebase" },
  { Log::NO_TOPIC , nullptr },
};

void MainOptions::parse(int argc, char *argv[]) {
  arguments.push_back(argv[0]);

#ifdef PIPY_DEFAULT_OPTIONS
  std::list<std::string> args = utils::split_argv(PIPY_DEFAULT_OPTIONS);
#else
  std::list<std::string> args;
#endif

  for (int i = 1; i < argc; i++) {
    std::string opt(argv[i]);
#ifdef PIPY_DEFAULT_OPTIONS
    if (opt == "--pipy") {
      args.clear();
      continue;
    }
#endif
    args.push_back(opt);
  }

  parse(args);
}

void MainOptions::parse(const std::list<std::string> &args) {
  auto max_threads = std::thread::hardware_concurrency();
  bool end_of_options = false;

  for (const auto &term : args) {
    if (end_of_options) {
      if (term == "--pipy-options") {
        end_of_options = false;
      } else {
        if (term[0] != '-' && filename.empty()) {
          filename = term;
        } else {
          arguments.push_back(term);
        }
      }
      continue;
    }
    if (term[0] != '-') {
      if (filename.empty()) {
        filename = term;
      } else {
        throw std::runtime_error("redundant argument: " + term);
      }
    } else {
      auto i = term.find('=');
      auto k = (i == std::string::npos ? term : term.substr(0, i));
      auto v = (i == std::string::npos ? std::string() : term.substr(i + 1));
      if (k == "--" || k == "-args" || k == "--args") {
        end_of_options = true;
      } else if (k == "-v" || k == "-version" || k == "--version") {
        version = true;
      } else if (k == "-h" || k == "-help" || k == "--help") {
        help = true;
      } else if (k == "-e" || k == "-eval" || k == "--eval") {
        eval = true;
      } else if (k == "-f" || k == "-file" || k == "--file") {
        file = true;
      } else if (k == "--threads") {
        if (v == "max") {
          threads = max_threads;
        } else {
          char *end;
          threads = std::strtol(v.c_str(), &end, 10);
          if (*end) throw std::runtime_error("--threads expects a number");
          if (threads <= 0) throw std::runtime_error("invalid number of threads");
          if (threads > max_threads) {
            std::string msg("number of threads exceeds the maximum ");
            throw std::runtime_error(msg + std::to_string(max_threads));
          }
        }
      } else if (k == "--log-file") {
        log_file = v;
      } else if (k == "--log-file-max-size") {
        log_file_max_size = utils::get_binary_size(v);
      } else if (k == "--log-file-max-count") {
        log_file_max_count = std::atoi(v.c_str());
      } else if (k == "--log-file-rotate-interval") {
        log_file_rotate_interval = utils::get_seconds(v);
      } else if (k == "--log-level") {
        if (
          utils::starts_with(v, "debug") && (
          v.length() == 5 ||
          v.at(5) == ':')
        ) {
          log_level = Log::DEBUG;
          if (v.length() == 5) {
            log_topics = 0xffffffff;
          } else {
            log_topics = 0;
            for (const auto &topic : utils::split(v.substr(6), '+')) {
              int mask = 0;
              for (int i = 0; s_topic_names[i].name; i++) {
                if (topic == s_topic_names[i].name) {
                  mask = s_topic_names[i].topic;
                  break;
                }
              }
              if (!mask) {
                std::string msg("unknown log topic: ");
                msg += topic;
                msg += " (available topics include: ";
                for (int i = 0; s_topic_names[i].name; i++) {
                  if (i > 0) msg += " | ";
                  msg += s_topic_names[i].name;
                }
                msg += ')';
                throw std::runtime_error(msg);
              }
              log_topics |= mask;
            }
          }
        }
        else if (v == "warn") log_level = Log::WARN;
        else if (v == "error") log_level = Log::ERROR;
        else if (v == "info") log_level = Log::INFO;
        else throw std::runtime_error("unknown log level: " + v);
      } else if (k == "--log-history-limit") {
        log_history_limit = utils::get_binary_size(v);
      } else if (k == "--log-local") {
        if (v == "null") log_local = Log::OUTPUT_NULL;
        else if (v == "stdout") log_local = Log::OUTPUT_STDOUT;
        else if (v == "stderr") log_local = Log::OUTPUT_STDERR;
        else throw std::runtime_error("unknown log output: " + v);
      } else if (k == "--log-local-only") {
        log_local_only = true;
      } else if (k == "--trace-objects") {
        trace_objects = true;
      } else if (k == "--instance-uuid") {
        instance_uuid = v;
      } else if (k == "--instance-name") {
        instance_name = v;
      } else if (k == "--reuse-port") {
        reuse_port = true;
      } else if (k == "--admin-port-off") {
        admin_port_off = true;
      } else if (k == "--admin-port") {
        admin_port = v;
      } else if (k == "--admin-tls-cert") {
        admin_tls_cert = load_certificate(v);
      } else if (k == "--admin-tls-key") {
        admin_tls_key = load_private_key(v);
      } else if (k == "--admin-tls-trusted") {
        load_certificate_list(v, admin_tls_trusted);
      } else if (k == "--tls-cert") {
        tls_cert = load_certificate(v);
      } else if (k == "--tls-key") {
        tls_key = load_private_key(v);
      } else if (k == "--tls-trusted") {
        load_certificate_list(v, tls_trusted);
      } else if (k == "--openssl-engine") {
        openssl_engine = v;
      } else {
        throw std::runtime_error("unknown option: " + k);
      }
    }
  }

  if (eval && filename.empty()) {
    throw std::runtime_error("missing script to evaluate");
  }

  if (log_history_limit > 256*1024*1024) {
    throw std::runtime_error("maximum value supported by --log-history-limit is 256MB");
  }

  if (!instance_uuid.empty() && instance_uuid.find('/') != std::string::npos) {
    throw std::runtime_error("--instance-uuid does not allow slashes");
  }

  if (!admin_port.empty()) {
    std::string host;
    int port;
    if (utils::get_host_port(admin_port, host, port)) {
      uint8_t ip[16];
      if (!host.empty() && !utils::get_ip_v4(host.c_str(), ip) && !utils::get_ip_v6(host.c_str(), ip)) {
        throw std::runtime_error("invalid --admin-port");
      }
    } else {
      char *ptr_end;
      if (std::strtol(admin_port.c_str(), &ptr_end, 10) <= 0 || *ptr_end) {
        throw std::runtime_error("invalid --admin-port");
      }
    }
  }

  if (bool(admin_tls_cert) != bool(admin_tls_key)) {
    throw std::runtime_error("--admin-tls-cert and --admin-tls-key must be used in conjunction");
  }

  if (!admin_tls_trusted.empty() && !admin_tls_cert) {
    throw std::runtime_error("--admin-tls-cert and --admin-tls-key are required for --admin-tls-trusted");
  }

  if (bool(tls_cert) != bool(tls_key)) {
    throw std::runtime_error("--tls-cert and --tls-key must be used in conjunction");
  }
}

auto MainOptions::load_private_key(const std::string &filename) -> crypto::PrivateKey* {
  std::vector<uint8_t> buf;
  if (!fs::read_file(filename, buf)) {
    std::string msg("cannot open file: ");
    throw std::runtime_error(msg + filename);
  }
  pjs::Ref<Data> data = s_dp.make(&buf[0], buf.size());
  return crypto::PrivateKey::make(data);
}

auto MainOptions::load_certificate(const std::string &filename) -> crypto::Certificate* {
  std::vector<uint8_t> buf;
  if (!fs::read_file(filename, buf)) {
    std::string msg("cannot open file: ");
    throw std::runtime_error(msg + filename);
  }
  pjs::Ref<Data> data = s_dp.make(&buf[0], buf.size());
  return crypto::Certificate::make(data);
}

void MainOptions::load_certificate_list(const std::string &filename, std::vector<pjs::Ref<crypto::Certificate>> &list) {
  if (fs::is_file(filename)) {
    list.push_back(load_certificate(filename));

  } else if (fs::is_dir(filename)) {
    std::list<std::string> names;
    if (!fs::read_dir(filename, names)) {
      std::string msg("cannot read directory: ");
      throw std::runtime_error(msg + filename);
    }
    for (const auto &name : names) {
      list.push_back(load_certificate(
        utils::path_join(filename, name)
      ));
    }
  } else {
    std::string msg("file or directory not found: ");
    throw std::runtime_error(msg + filename);
  }
}

} // namespace pipy
