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

#include <cstdarg>
#include <cstdio>
#include <ctime>

NS_BEGIN

//
// Logging
//

static Log::Level s_log_level = Log::ERROR;

static void log(Log::Level level, const char *fmt, va_list ap) {
  if (level >= s_log_level) {
    char time_str[100];
    std::time_t t;
    std::time(&t);
    std::strftime(time_str, sizeof(time_str), "%c", std::localtime(&t));
    switch (level) {
      case Log::DEBUG: printf("%s [debug] ", time_str); break;
      case Log::INFO : printf("%s [info] ", time_str); break;
      case Log::WARN : printf("%s [warning] ", time_str); break;
      case Log::ERROR: printf("%s [error] ", time_str); break;
    }
    vprintf(fmt, ap);
    printf("\n");
  }
}

void Log::set_level(Level level) {
  s_log_level = level;
}

void Log::debug(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  log(Log::DEBUG, fmt, ap);
  va_end(ap);
}

void Log::info(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  log(Log::INFO, fmt, ap);
  va_end(ap);
}

void Log::warn(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  log(Log::WARN, fmt, ap);
  va_end(ap);
}

void Log::error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  log(Log::ERROR, fmt, ap);
  va_end(ap);
}

NS_END
