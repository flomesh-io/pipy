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

#ifndef FSTREAM_HPP
#define FSTREAM_HPP

#include "net.hpp"
#include "event.hpp"

namespace pipy {

class Data;

//
// FileStream
//

class FileStream : public pjs::Pooled<FileStream> {
public:
  FileStream(int fd, Data::Producer *dp);
  ~FileStream();

  auto fd() const -> int { return m_fd; }

  void set_buffer_limit(size_t size) { m_buffer_limit = size; }

  void on_read(EventTarget::Input *input) { m_reader = input; }
  void on_delete(const std::function<void()> &callback) { m_on_delete = callback; }

  void write(const pjs::Ref<Data> &data);
  void flush();
  void end();

private:
  int m_fd;
  Data::Producer* m_dp;
  asio::posix::stream_descriptor m_stream;
  pjs::Ref<EventTarget::Input> m_reader;
  std::function<void()> m_on_delete;
  Data m_buffer;
  size_t m_buffer_limit = 0;
  bool m_overflowed = false;
  bool m_pumping = false;
  bool m_reading_ended = false;
  bool m_writing_ended = false;

  void read();
  void pump();
  void close();
  void free();
};

} // namespace pipy

#endif // FSTREAM_HPP
