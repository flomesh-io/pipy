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

#ifndef FILE_HPP
#define FILE_HPP

#include "data.hpp"
#include "fstream.hpp"

#include <fstream>
#include <stdio.h>

namespace pipy {

//
// File
//

class File :
  public pjs::RefCount<File>,
  public pjs::Pooled<File>
{
public:
  static auto make(const std::string &path) -> File* {
    return new File(path);
  }

  void open_read(const std::function<void(FileStream*)> &cb);
  void open_read(int seek, const std::function<void(FileStream*)> &cb);
  void open_write();
  void write(const Data &data);
  void close();
  void unlink();

private:
  File(const std::string &path)
    : m_path(path) {}

  ~File() {}

  std::string m_path;
  FILE* m_f = nullptr;
  Data m_buffer;
  pjs::Ref<FileStream> m_stream;
  bool m_writing = false;
  bool m_closed = false;

  bool mkdir_p(const std::string &path);

  friend class pjs::RefCount<File>;
};

} // namespace pipy

#endif // FILE_HPP
