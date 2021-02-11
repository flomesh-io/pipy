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

NS_BEGIN

void Options::show_help() {
  std::cout << "Usage: pipy [options] <config filename>" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -h, -help, --help                    Show help information" << std::endl;
  std::cout << "  -v, -version, --version              Show version information" << std::endl;
  std::cout << "  --list-modules                       List all supported modules" << std::endl;
  std::cout << "  --help-modules                       Show help information for all supported modules" << std::endl;
  std::cout << "  --log-level=<debug|info|warn|error>  Set the level of log output" << std::endl;
  std::cout << "  --verify                             Verify configuration only" << std::endl;
  std::cout << "  --watch-config-file                  Reload configuration when config file changes" << std::endl;
  std::cout << "  --reuse-port                         Enable kernel load balancing for all listening ports" << std::endl;
  std::cout << std::endl;
}

Options::Options(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    std::string term(argv[i]);
    if (term[0] != '-') {
      if (!config_filename.empty()) {
        std::string msg("redundant config filename: ");
        throw std::runtime_error(msg + term);
      }
      config_filename = term;
    } else {
      auto i = term.find('=');
      auto k = (i == std::string::npos ? term : term.substr(0, i));
      auto v = (i == std::string::npos ? std::string() : term.substr(i + 1));
      if (k == "-v" || k == "-version" || k == "--version") {
        version = true;
      } else if (k == "-h" || k == "-help" || k == "--help") {
        help = true;
      } else if (k == "--help-modules") {
        help_modules = true;
      } else if (k == "--list-modules") {
        list_modules = true;
      } else if (k == "--log-level") {
        if (v == "debug") log_level = Log::DEBUG;
        else if (v == "info") log_level = Log::INFO;
        else if (v == "warn") log_level = Log::WARN;
        else if (v == "error") log_level = Log::ERROR;
        else {
          std::string msg("unknown log level: ");
          throw std::runtime_error(msg + v);
        }
      } else if (k == "--verify") {
        verify = true;
      } else if (k == "--watch-config-file") {
        watch_config_file = true;
      } else if (k == "--reuse-port") {
        reuse_port = true;
      } else {
        std::string msg("unknown option: ");
        throw std::runtime_error(msg + k);
      }
    }
  }
}

NS_END