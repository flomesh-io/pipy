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

static std::string s_log_filename;
static Log::Level s_log_level = Log::ERROR;
static int s_log_topics = 0;
static bool s_log_local_only = false;

thread_local static logging::Logger *s_logger = nullptr;
thread_local static Data::Producer s_dp("Log");

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
    if (s_is_logging || s_log_local_only) {
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
  s_logger->retain();
  s_logger->add_target(new logging::Logger::StdoutTarget(stderr));
  if (!s_log_filename.empty()) {
    pjs::Ref<pjs::Str> s(pjs::Str::make(s_log_filename));
    s_logger->add_target(new logging::Logger::FileTarget(s));
  }
}

void Log::shutdown() {
  s_logger->release();
  s_logger = nullptr;
}

void Log::set_filename(const std::string &filename) {
  s_log_filename = filename;
}

void Log::set_level(Level level) {
  s_log_level = level;
}

void Log::set_topics(int topics) {
  s_log_topics = topics;
}

void Log::set_local_only(bool b) {
  s_log_local_only = b;
}

bool Log::is_enabled(Level level) {
  return (level >= s_log_level);
}

bool Log::is_enabled(Topic topic) {
  return (s_log_level <= DEBUG) && (s_log_topics & topic);
}

auto Log::format_elapsed_time() -> const char* {
  thread_local static char s_buf[12];
  format_elapsed_time(s_buf, sizeof(s_buf), true);
  return s_buf;
}

auto Log::format_elapsed_time(char *buf, size_t len, bool fill) -> size_t {
  thread_local static std::chrono::high_resolution_clock::time_point s_time;
  thread_local static bool s_started = false;

  auto t = std::chrono::high_resolution_clock::now();
  auto d = s_started ? std::chrono::duration_cast<std::chrono::microseconds>(t - s_time).count() : 0;
  auto p = 0;

  s_time = t;
  s_started = true;

  if (d >= 1000000) {
    p = std::snprintf(buf, len, "T+%.2fs", (double)d / 1000000);
  } else if (d >= 1000) {
    p = std::snprintf(buf, len, "T+%.2fms", (double)d / 1000);
  } else {
    p = std::snprintf(buf, len, "T+%d", (int)d);
  }

  if (fill) {
    while (p + 1 < len) buf[p++] = ' ';
  }

  buf[p] = 0;
  return p;
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

auto Log::format_location(char *buf, size_t len, const pjs::Context::Location &loc, const char *func_name) -> size_t {
  auto source = loc.source;
  if (!source || source->filename.empty()) {
    return std::snprintf(
      buf, len,
      "%s() at line %d column %d",
      func_name,
      loc.line,
      loc.column
    );
  } else {
    return std::snprintf(
      buf, len,
      "%s() at line %d column %d in %s",
      func_name,
      loc.line,
      loc.column,
      loc.source->filename.c_str()
    );
  }
}

void Log::write(const Data &data) {
  if (s_log_local_only) {
    for (const auto &c : data.chunks()) {
      auto ptr = std::get<0>(c);
      auto len = std::get<1>(c);
      std::cerr.write(ptr, len);
    }
    std::cerr << std::endl;
  } else {
    s_logger->write(data);
  }
}

void Log::write(const std::string &data) {
  if (s_log_local_only) {
    std::cerr << data << std::endl;
  } else {
    Data buf;
    s_dp.push(&buf, data);
    s_logger->write(buf);
  }
}

void Log::debug(Topic topic, const char *fmt, ...) {
  if (is_enabled(topic)) {
    va_list ap;
    va_start(ap, fmt);
    logf(Log::DEBUG, fmt, ap);
    va_end(ap);
  }
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

void Log::pjs_location(const std::string &source, const std::string &filename, int line, int column) {
  if (line > 0 && column > 0) {
    size_t i = 0;
    for (int l = 1; l < line; l++, i++) {
      while (i < source.length() && source[i] != '\n') i++;
      if (source[i] != '\n') return;
    }
    while (i < source.length() && std::isblank(source[i]) && column > 1) { i++; column--; }
    size_t j = i;
    while (j < source.length() && source[j] != '\n') j++;
    auto str = source.substr(i, j - i);
    auto num = std::to_string(line);
    if (!filename.empty()) error("[pjs] File %s:", filename.c_str());
    error("[pjs] Line %s:  %s" , num.c_str(), str.c_str());
    error("[pjs]      %s   %s^", std::string(num.length(), ' ').c_str(), std::string(column - 1, ' ').c_str());
  }
}

void Log::pjs_error(const pjs::Context::Error &err) {
  if (auto *loc = err.where()) {
    const auto *src = loc->source;
    pjs_location(src->content, src->filename, loc->line, loc->column);
  }
  error("[pjs] Error: %s", err.message.c_str());
  error("[pjs] Backtrace:");
  for (const auto &l : err.backtrace) {
    std::string str("In ");
    str += l.name;
    if (l.line && l.column) {
      char s[100];
      std::sprintf(s, " at line %d column %d in %s", l.line, l.column, l.source->filename.c_str());
      str += s;
    }
    error("    %s", str.c_str());
  }
}

} // namespace pipy
