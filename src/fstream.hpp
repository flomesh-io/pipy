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

#include <stdio.h>

#include "event.hpp"
#include "input.hpp"
#include "net.hpp"

namespace pipy {

class Data;

//
// FileStream
//

class FileStream :
  public pjs::RefCount<FileStream>,
                   public pjs::Pooled<FileStream>,
                   public EventFunction,
                   public InputSource,
                   public FlushTarget
{
 public:

  static auto make(bool read, FILE *f, Data::Producer *dp) -> FileStream* {
    return new FileStream(read, f, dp);
  }

#ifndef _WIN32
  static auto make(bool read, int fd, Data::Producer *dp) -> FileStream * {
    return new FileStream(read, fd, dp);
  }
  auto fd() const -> int { return m_fd; }
#else
  static auto make(bool read, HANDLE fd, Data::Producer *dp) -> FileStream * {
    return new FileStream(read, fd, dp);
  }
  auto fd() const -> HANDLE { return m_fd; }
#endif
  void set_buffer_limit(size_t size) { m_buffer_limit = size; }
  void close(bool close_fd = true);

 private:
  FileStream(bool read, FILE *f, Data::Producer *dp);
#ifndef _WIN32
  FileStream(bool read, int fd, Data::Producer *dp);
#else
  FileStream(bool read, HANDLE fd, Data::Producer *dp);
#endif
  virtual void on_event(Event *evt) override;
  virtual void on_flush() override;
  virtual void on_tap_open() override;
  virtual void on_tap_close() override;

  enum ReceivingState {
    RECEIVING,
    PAUSING,
    PAUSED,
  };

#if defined(_WIN32)
  asio::windows::stream_handle m_stream;
  HANDLE m_fd;
#else
  asio::posix::stream_descriptor m_stream;
  int m_fd;
#endif

  FILE* m_f;
  Data::Producer* m_dp;
  Data m_buffer;
  size_t m_buffer_limit = 0;
  ReceivingState m_receiving_state = RECEIVING;
  bool m_overflowed = false;
  bool m_pumping = false;
  bool m_ended = false;

  void read();
  void write(Data *data);
  void end();
  void pump();

  friend class pjs::RefCount<FileStream>;
};

} // namespace pipy

#endif // FSTREAM_HPP
