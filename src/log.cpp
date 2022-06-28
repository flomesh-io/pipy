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

#include "log.hpp"
#include "data.hpp"
#include "api/logging.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <sstream>

namespace pipy {

//
// Logging
//

static Log::Level s_log_level = Log::ERROR;
static pjs::Ref<logging::Logger> s_logger;
static Data::Producer s_dp("Log");

static const char *s_levels[] = {
  "[DBG]", // DEBUG
  "[WRN]", // WARN
  "[ERR]", // ERROR
  "[INF]", // INFO
};

static void logf(Log::Level level, const char *fmt, va_list ap) {
  static bool s_is_logging = false;
  if (Log::is_enabled(level)) {
    char header[100], msg[1000];
    if (s_is_logging) {
      Log::format_header(level, header, sizeof(header));
      std::vsnprintf(msg, sizeof(msg), fmt, ap);
      std::cerr << header << msg << std::endl;
    } else {
      s_is_logging = true;
      Data buf;
      Data::Builder db(buf, &s_dp);
      db.push(header, Log::format_header(level, header, sizeof(header)));
      db.push(msg, std::vsnprintf(msg, sizeof(msg), fmt, ap));
      db.flush();
      s_logger->write(buf);
      s_is_logging = false;
    }
  }
}

void Log::init() {
  s_logger = logging::TextLogger::make(pjs::Str::make("pipy_log"));
  s_logger->add_target(new logging::Logger::StdoutTarget(stderr));
}

void Log::set_level(Level level) {
  s_log_level = level;
}

bool Log::is_enabled(Level level) {
  return (level >= s_log_level);
}

auto Log::format_header(Level level, char *buf, size_t len) -> size_t {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto cnt = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  auto sec = cnt / 1000;
  auto msec = int(cnt % 1000);
  std::time_t t = sec;
  auto i = 0;
  i += std::strftime(buf + i, len - i, "%F %T", std::localtime(&t));
  i += std::snprintf(buf + i, len - i, ".%03d %s ", msec, s_levels[level]);
  return i;
}

void Log::write(const Data &data) {
  s_logger->write(data);
}

void Log::write(const std::string &data) {
  Data buf;
  s_dp.push(&buf, data);
  s_logger->write(buf);
}

void Log::debug(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logf(Log::DEBUG, fmt, ap);
  va_end(ap);
}

void Log::info(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logf(Log::INFO, fmt, ap);
  va_end(ap);
}

void Log::warn(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logf(Log::WARN, fmt, ap);
  va_end(ap);
}

void Log::error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logf(Log::ERROR, fmt, ap);
  va_end(ap);
}

void Log::pjs_location(const std::string &source, int line, int column) {
  if (line > 0 && column > 0) {
    size_t i = 0;
    for (int l = 1; l < line; l++, i++) {
      while (i < source.length() && source[i] != '\n') i++;
      if (source[i] != '\n') return;
    }
    while (i < source.length() && std::isblank(source[i])) { i++; column--; }
    size_t j = i;
    while (j < source.length() && source[j] != '\n') j++;
    auto str = source.substr(i, j - i);
    auto num = std::to_string(line);
    error("[pjs] Line %s:  %s" , num.c_str(), str.c_str());
    error("[pjs]      %s   %s^", std::string(num.length(), ' ').c_str(), std::string(column - 1, ' ').c_str());
  }
}

void Log::pjs_error(const pjs::Context::Error &err) {
  error("[pjs] Error: %s", err.message.c_str());
  error("[pjs] Backtrace:");
  for (const auto &l : err.backtrace) {
    std::string str("In ");
    str += l.name;
    if (l.line && l.column) {
      char s[100];
      std::sprintf(s, " at line %d column %d", l.line, l.column);
      str += s;
    }
    error("    %s", str.c_str());
  }
}

void Log::pjs_error(const pjs::Context::Error &err, const std::string &source) {
  if (auto *loc = err.where()) {
    pjs_location(source, loc->line, loc->column);
  }
  pjs_error(err);
}

} // namespace pipy
