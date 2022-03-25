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

#include "file.hpp"
#include "fstream.hpp"
#include "fs.hpp"
#include "net.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <chrono>
#include <functional>

namespace pipy {

static Data::Producer s_dp("File I/O");
static asio::thread_pool* s_thread_pool = nullptr;

void File::start_bg_thread() {
  s_thread_pool = new asio::thread_pool();
}

void File::stop_bg_thread() {
  s_thread_pool->join();
}

void File::open_read(const std::function<void(FileStream*)> &cb) {
  open_read(0, cb);
}

void File::open_read(int seek, const std::function<void(FileStream*)> &cb) {
  if (m_f || m_closed) return;

  std::string path = m_path;

  retain();
  asio::post(
    *s_thread_pool,
    [=]() {
      if (auto f = (path == "-" ? stdin : fopen(path.c_str(), "rb"))) {
        if (seek > 0) fseek(f, seek, SEEK_SET);
        Net::post(
          [=]() {
            m_f = f;
            m_stream = FileStream::make(f, &s_dp);
            if (m_closed) {
              close();
            }
            cb(m_stream);
            release();
          }
        );
      } else {
        Net::post(
          [=]() {
            Log::error("[file] cannot open file for reading: %s", m_path.c_str());
            release();
          }
        );
      }
    }
  );
}

void File::open_write() {
  if (m_f || m_closed) return;

  std::string path = m_path;

  retain();
  asio::post(
    *s_thread_pool,
    [=]() {
      auto dirname = utils::path_dirname(path);
      if (!dirname.empty() && !mkdir_p(dirname)) {
        Net::post(
          [=]() {
            Log::error("[file] cannot create directory: %s", dirname.c_str());
            release();
          }
        );
      } else if (auto f = fopen(path.c_str(), "wb")) {
        Net::post(
          [=]() {
            m_f = f;
            m_writing = true;
            m_stream = FileStream::make(f, &s_dp);
            if (!m_buffer.empty()) {
              m_stream->input()->input(Data::make(m_buffer));
              m_stream->input()->input(Data::flush());
              m_buffer.clear();
            }
            if (m_closed) {
              close();
            }
            release();
          }
        );
      } else {
        Net::post(
          [=]() {
            Log::error("[file] cannot open file for writing: %s", m_path.c_str());
            release();
          }
        );
      }
    }
  );
}

void File::write(const Data &data) {
  if (m_closed) return;
  if (m_stream) {
    m_stream->input()->input(Data::make(data));
  } else {
    m_buffer.push(data);
  }
}

void File::close() {
  if (m_stream) {
    if (m_writing) {
      m_stream->input()->input(StreamEnd::make());
    }
    m_stream = nullptr;
    m_f = nullptr;
  }
  m_closed = true;
}

bool File::mkdir_p(const std::string &path) {
  if (fs::is_dir(path)) return true;
  auto dirname = utils::path_dirname(path);
  if (!mkdir_p(dirname)) return false;
  return fs::make_dir(path);
}

} // namespace pipy
