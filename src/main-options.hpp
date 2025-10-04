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

#ifndef MAIN_OPTIONS_HPP
#define MAIN_OPTIONS_HPP

#include "api/crypto.hpp"
#include "log.hpp"

#include <list>
#include <string>
#include <vector>

namespace pipy {

//
// MainOptions
//

class MainOptions {
public:
  static void show_help();

  std::vector<std::string> arguments;

  std::string filename;
  bool        version = false;
  bool        help = false;
  bool        file = false;
  bool        eval = false;
  bool        trace_objects = false;
  bool        reuse_port = false;
  int         threads = 1;
  std::string log_file;
  int         log_file_max_size = 0;
  int         log_file_max_count = 0;
  double      log_file_rotate_interval = 0;
  Log::Level  log_level = Log::INFO;
  Log::Output log_local = Log::OUTPUT_STDERR;
  size_t      log_history_limit = 1024*1024;
  int         log_topics = 0;
  bool        log_local_only = false;
  bool        admin_port_off = false;
  std::string admin_port;
  std::string instance_uuid;
  std::string instance_name;
  std::string openssl_engine;

  pjs::Ref<crypto::Certificate>               admin_tls_cert;
  pjs::Ref<crypto::PrivateKey>                admin_tls_key;
  std::vector<pjs::Ref<crypto::Certificate>>  admin_tls_trusted;
  pjs::Ref<crypto::Certificate>               tls_cert;
  pjs::Ref<crypto::PrivateKey>                tls_key;
  std::vector<pjs::Ref<crypto::Certificate>>  tls_trusted;

  void parse(int argc, char *argv[]);
  void parse(const std::list<std::string> &args);

private:
  auto load_private_key(const std::string &filename) -> crypto::PrivateKey*;
  auto load_certificate(const std::string &filename) -> crypto::Certificate*;
  void load_certificate_list(const std::string &filename, std::vector<pjs::Ref<crypto::Certificate>> &list);
};

} // namespace pipy

#endif // MAIN_OPTIONS_HPP
