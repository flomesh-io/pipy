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

#ifndef UTILS_HPP
#define UTILS_HPP

#include "ns.hpp"

#include <list>
#include <map>
#include <stdexcept>
#include <string>

NS_BEGIN

namespace utils {

auto get_param(const std::map<std::string, std::string> &params, const char *name, const char *value = nullptr) -> std::string;
bool get_host_port(const std::string &str, std::string &ip, int &port);
bool get_ip_port(const std::string &str, std::string &ip, int &port);
auto get_file_time(const std::string &filename) -> uint64_t;
auto get_byte_size(const std::string &str) -> size_t;
auto get_seconds(const std::string &str) -> double;
auto trim(const std::string &str) -> std::string;
auto split(const std::string &str, char sep) -> std::list<std::string>;
auto lower(const std::string &str) -> std::string;
auto escape(const std::string &str) -> std::string;
auto unescape(const std::string &str) -> std::string;
auto path_join(const std::string &base, const std::string &path) -> std::string;

} // namespace utils

NS_END

#endif // UTILS_HPP