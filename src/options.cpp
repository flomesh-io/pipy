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

#include "options.hpp"

#include <iostream>

namespace pipy {

void Options::show_help() {
  std::cout << "Usage: pipy [options] <script filename>" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -h, -help, --help                    Show help information" << std::endl;
  std::cout << "  -v, -version, --version              Show version information" << std::endl;
  std::cout << "  --list-filters                       List all filters" << std::endl;
  std::cout << "  --help-filters                       Show detailed usage information for all filters" << std::endl;
  std::cout << "  --log-level=<debug|info|warn|error>  Set the level of log output" << std::endl;
  std::cout << "  --verify                             Verify configuration only" << std::endl;
  std::cout << "  --reuse-port                         Enable kernel load balancing for all listening ports" << std::endl;
  std::cout << "  --dev-port=<port>                    Enable development service on the specified port" << std::endl;
  std::cout << "  --repo-port=<port>                   Enable repository service on the specified port" << std::endl;
  std::cout << std::endl;
}

Options::Options(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    std::string term(argv[i]);
    if (term[0] != '-') {
      if (!filename.empty()) {
        std::string msg("redundant filename: ");
        throw std::runtime_error(msg + term);
      }
      filename = term;
    } else {
      auto i = term.find('=');
      auto k = (i == std::string::npos ? term : term.substr(0, i));
      auto v = (i == std::string::npos ? std::string() : term.substr(i + 1));
      if (k == "-v" || k == "-version" || k == "--version") {
        version = true;
      } else if (k == "-h" || k == "-help" || k == "--help") {
        help = true;
      } else if (k == "--help-filters") {
        help_filters = true;
      } else if (k == "--list-filters") {
        list_filters = true;
      } else if (k == "--log-level") {
        if (v == "debug") log_level = Log::DEBUG;
        else if (v == "warn") log_level = Log::WARN;
        else if (v == "error") log_level = Log::ERROR;
        else if (v == "info") log_level = Log::INFO;
        else {
          std::string msg("unknown log level: ");
          throw std::runtime_error(msg + v);
        }
      } else if (k == "--verify") {
        verify = true;
      } else if (k == "--reuse-port") {
        reuse_port = true;
      } else if (k == "--dev-port") {
        dev_port = std::atoi(v.c_str());
      } else if (k == "--repo-port") {
        repo_port = std::atoi(v.c_str());
      } else {
        std::string msg("unknown option: ");
        throw std::runtime_error(msg + k);
      }
    }
  }

  if (dev_port < 0) throw std::runtime_error("invalid --dev-port");
  if (repo_port < 0) throw std::runtime_error("invalid --repo-port");
  if (repo_port && dev_port) throw std::runtime_error("options --dev-port and --repo-port are mutually exclusive");
}

} // namespace pipy