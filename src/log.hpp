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

#ifndef LOG_H
#define LOG_H

#include "pjs/pjs.hpp"

namespace pipy {

class Data;

//
// Log
//

class Log {
public:
  enum Output {
    OUTPUT_NULL,
    OUTPUT_STDOUT,
    OUTPUT_STDERR,
  };

  enum Level {
    DEBUG,
    WARN,
    INFO,
    ERROR,
  };

  enum Topic {
    NO_TOPIC  = 0,
    ALLOC     = 1<<0,
    THREAD    = 1<<1,
    PIPELINE  = 1<<2,
    DUMP      = 1<<3,
    LISTENER  = 1<<4,
    INBOUND   = 1<<5,
    OUTBOUND  = 1<<6,
    SOCKET    = 1<<7,
    FILES     = 1<<8,
    SUBPROC   = 1<<9,
    NETLINK   = 1<<10,
    TCP       = 1<<11,
    UDP       = 1<<12,
    HTTP2     = 1<<13,
    ELF       = 1<<14,
    BPF       = 1<<15,
    USER      = 1<<16,
  };

  static void init();
  static void shutdown();
  static void set_filename(const std::string &filename);
  static void set_rotate(double interval, int max_file_size, int max_file_count);
  static void set_level(Level level);
  static void set_topics(int topics);
  static void set_local_output(Output output);
  static void set_local_only(bool b);
  static bool is_enabled(Level level);
  static bool is_enabled(Topic topic);

  static auto format_elapsed_time() -> const char*;
  static auto format_elapsed_time(char *buf, size_t len, bool fill = false) -> size_t;
  static auto format_time(char *buf, size_t len) -> size_t;
  static auto format_header(Level level, char *buf, size_t len) -> size_t;
  static auto format_location(char *buf, size_t len, const pjs::Location &loc, const char *func_name) -> size_t;

  static void write(const Data &data);
  static void write(const std::string &data);

  static void debug(Topic topic, const char *fmt, ...);
  static void info(const char *fmt, ...);
  static void warn(const char *fmt, ...);
  static void error(const char *fmt, ...);

  static void pjs_location(const std::string &source, const std::string &filename, int line, int column);
  static void pjs_error(const pjs::Context::Error &err);
};

} // namespace pipy

#endif // LOG_H
