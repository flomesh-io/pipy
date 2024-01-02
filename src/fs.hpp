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

#ifndef FS_HPP
#define FS_HPP

#include <list>
#include <string>
#include <vector>

namespace pipy {
namespace fs {

auto abs_path(const std::string &filename) -> std::string;
bool exists(const std::string &filename);
bool is_dir(const std::string &filename);
bool is_file(const std::string &filename);
auto get_file_time(const std::string &filename) -> double;
void change_dir(const std::string &filename);
bool make_dir(const std::string &filename);
bool read_dir(const std::string &filename, std::list<std::string> &list);
bool read_file(const std::string &filename, std::vector<uint8_t> &data);
bool write_file(const std::string &filename, const std::vector<uint8_t> &data);
bool unlink(const std::string &filename);

} // namespace fs
} // namespace pipy

#endif // FS_HPP
