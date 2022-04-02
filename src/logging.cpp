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

#include "logging.hpp"

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
static std::deque<std::string> s_log_history;
static size_t s_log_history_size = 0;

static void write_history(const std::string &line) {
  s_log_history_size++;
  s_log_history.push_back(line);
  if (s_log_history.size() > 1000) s_log_history.pop_front();
}

static const char *s_levels[] = {
  " [DBG] ", // DEBUG
  " [WRN] ", // WARN
  " [ERR] ", // ERROR
  " [INF] ", // INFO
};

static void log(Log::Level level, const char *str) {
  if (Log::is_enabled(level)) {
    char time[100], ms[10];
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto cnt = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    auto sec = cnt / 1000;
    auto msec = int(cnt % 1000);
    std::time_t t = sec;
    std::strftime(time, sizeof(time), "%F %T", std::localtime(&t));
    std::snprintf(ms, sizeof(ms), ".%03d", msec);
    const char *type = s_levels[level];
    const char *p = str;
    auto &so = std::cerr;
    do {
      std::stringstream ss;
      so << time << ms << type;
      ss << time << ms << type;
      while (*p && *p != '\n') {
        auto c = *p++;
        so << c;
        ss << c;
      }
      if (*p) p++;
      write_history(ss.str());
      so << std::endl;
    } while (*p);
  }
}

static void logf(Log::Level level, const char *fmt, va_list ap) {
  if (Log::is_enabled(level)) {
    char msg[1000];
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    log(level, msg);
  }
}

void Log::set_level(Level level) {
  s_log_level = level;
}

bool Log::is_enabled(Level level) {
  return (level >= s_log_level);
}

void Log::print(const std::string &line) {
  std::cout << line << std::endl;
  write_history(line);
}

void Log::print(Level level, const char *msg) {
  log(level, msg);
}

void Log::print(Level level, const std::string &msg) {
  log(level, msg.c_str());
}

void Log::print(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char line[1000];
  std::vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  std::cout << line << std::endl;
  write_history(line);
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

auto Log::tail(size_t first, std::string &log) -> size_t {
  log.clear();
  if (first < s_log_history_size) {
    auto size = s_log_history_size - first;
    auto start = (size >= s_log_history.size() ? 0 : s_log_history.size() - size);
    for (size_t i = start; i < s_log_history.size(); i++) {
      log += s_log_history[i];
      log += '\n';
    }
  }
  return s_log_history_size;
}

} // namespace pipy
