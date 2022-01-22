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

#include "builtin.hpp"

#include <chrono>
#include <cmath>
#include <sstream>
#include <stack>

namespace pjs {

//
// Date
//

auto Date::now() -> double {
  auto t = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
  return double(ms);
}

Date::Date()
  : Date(now())
{
}

Date::Date(const Date *date)
  : m_tm(date->m_tm)
  , m_msec(date->m_msec)
{
}

Date::Date(double value) {
  setTime(value);
}

Date::Date(int year, int mon, int day, int hour, int min, int sec, int ms) {
  m_tm.tm_year = year - 1900;
  m_tm.tm_mon = mon;
  m_tm.tm_mday = day;
  m_tm.tm_hour = hour;
  m_tm.tm_min = min;
  m_tm.tm_sec = sec;
  setMilliseconds(ms);
}

auto Date::getTime() -> double {
  auto t = std::mktime(&m_tm);
  return double(t) * 1000 + m_msec;
}

auto Date::setDate(int value) -> double {
  m_tm.tm_mday = value;
  return normalize();
}

auto Date::setFullYear(int value) -> double {
  m_tm.tm_year = value - 1900;
  return normalize();
}

auto Date::setFullYear(int y, int m) -> double {
  m_tm.tm_year = y - 1900;
  m_tm.tm_mon = m;
  return normalize();
}

auto Date::setFullYear(int y, int m, int d) -> double {
  m_tm.tm_year = y - 1900;
  m_tm.tm_mon = m;
  m_tm.tm_mday = d;
  return normalize();
}

auto Date::setHours(int value) -> double {
  m_tm.tm_hour = value;
  return normalize();
}

auto Date::setHours(int h, int m) -> double {
  m_tm.tm_hour = h;
  m_tm.tm_min = m;
  return normalize();
}

auto Date::setHours(int h, int m, int s) -> double {
  m_tm.tm_hour = h;
  m_tm.tm_min = m;
  m_tm.tm_sec = s;
  return normalize();
}

auto Date::setHours(int h, int m, int s, int ms) -> double {
  m_tm.tm_hour = h;
  m_tm.tm_min = m;
  m_tm.tm_sec = s;
  return setMilliseconds(ms);
}

auto Date::setMilliseconds(int value) -> double {
  int s = value / 1000;
  int ms = value % 1000;
  if (ms < 0) { ms += 1000; s--; }
  m_tm.tm_sec += s;
  m_msec = ms;
  return normalize();
}

auto Date::setMinutes(int value) -> double {
  m_tm.tm_min = value;
  return normalize();
}

auto Date::setMinutes(int m, int s) -> double {
  m_tm.tm_min = m;
  m_tm.tm_sec = s;
  return normalize();
}

auto Date::setMinutes(int m, int s, int ms) -> double {
  m_tm.tm_min = m;
  m_tm.tm_sec = s;
  return setMilliseconds(ms);
}

auto Date::setMonth(int value) -> double {
  m_tm.tm_mon = value;
  return normalize();
}

auto Date::setMonth(int m, int d) -> double {
  m_tm.tm_mon = m;
  m_tm.tm_mday = d;
  return normalize();
}

auto Date::setSeconds(int value) -> double {
  m_tm.tm_sec = value;
  return normalize();
}

auto Date::setSeconds(int s, int ms) -> double {
  m_tm.tm_sec = s;
  return setMilliseconds(ms);
}

auto Date::setTime(double value) -> double {
  auto sec = std::floor(value / 1000);
  auto t = std::time_t(sec);
  localtime_r(&t, &m_tm);
  m_msec = value - sec * 1000;
  return value;
}

auto Date::toDateString() -> std::string {
  char str[100];
  auto len = std::strftime(str, sizeof(str), "%a %b %e %Y", &m_tm);
  return std::string(str, len);
}

auto Date::toTimeString() -> std::string {
  char str[100];
  auto len = std::strftime(str, sizeof(str), "%H:%M:%S GMT%z %Z", &m_tm);
  return std::string(str, len);
}

auto Date::toISOString() -> std::string {
  char str[100];
  auto len = std::strftime(str, sizeof(str), "%Y-%m-%dT%H:%M:%S.000Z", &m_tm);
  str[20] = (m_msec % 1000) / 100 + '0';
  str[21] = (m_msec % 100) / 10 + '0';
  str[22] = (m_msec % 10) + '0';
  return std::string(str, len);
}

auto Date::toUTCString() -> std::string {
  char str[100];
  auto len = std::strftime(str, sizeof(str), "%a, %e %b %Y %H:%M:%S GMT", &m_tm);
  return std::string(str, len);
}

void Date::value_of(Value &out) {
  out.set(getTime());
}

auto Date::to_string() const -> std::string {
  char str[100];
  auto len = std::strftime(str, sizeof(str), "%c", &m_tm);
  return std::string(str, len);
}

auto Date::dump() -> Object* {
  return nullptr;
}

auto Date::normalize() -> double {
  auto t = std::mktime(&m_tm);
  localtime_r(&t, &m_tm);
  return double(t) * 1000 + m_msec;
}

template<> void ClassDef<Date>::init() {
  ctor([](Context &ctx) {
    Date *date;
    double value;
    int year, mon, day = 1, hour = 0, min = 0, sec = 0, ms = 0;
    if (ctx.try_arguments(1, &date)) {
      return Date::make(date);
    } else if (ctx.try_arguments(2, &year, &mon, &day, &hour, &min, &sec, &ms)) {
      return Date::make(year, mon, day, hour, min, sec, ms);
    } else if (ctx.try_arguments(1, &value)) {
      return Date::make(value);
    } else {
      return Date::make();
    }
  });

  method("getDate", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getDate());
  });

  method("getDay", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getDay());
  });

  method("getFullYear", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getFullYear());
  });

  method("getHours", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getHours());
  });

  method("getMilliseconds", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getMilliseconds());
  });

  method("getMinutes", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getMinutes());
  });

  method("getMonth", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getMonth());
  });

  method("getSeconds", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getSeconds());
  });

  method("getTime", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Date>()->getTime());
  });

  method("setDate", [](Context &ctx, Object *obj, Value &ret) {
    double value;
    if (!ctx.arguments(1, &value)) return;
    ret.set(obj->as<Date>()->setDate(value));
  });

  method("setFullYear", [](Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() >= 3) {
      int y, m, d;
      if (!ctx.arguments(3, &y, &m, &d)) return;
      ret.set(obj->as<Date>()->setFullYear(y, m, d));
    } else if (ctx.argc() == 2) {
      int y, m;
      if (!ctx.arguments(2, &y, &m)) return;
      ret.set(obj->as<Date>()->setFullYear(y, m));
    } else {
      int value;
      if (!ctx.arguments(1, &value)) return;
      ret.set(obj->as<Date>()->setFullYear(value));
    }
  });

  method("setHours", [](Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() >= 4) {
      int h, m, s, ms;
      if (!ctx.arguments(4, &h, &m, &s, &ms)) return;
      ret.set(obj->as<Date>()->setHours(h, m, s, ms));
    } else if (ctx.argc() == 3) {
      int h, m, s;
      if (!ctx.arguments(3, &h, &m, &s)) return;
      ret.set(obj->as<Date>()->setHours(h, m, s));
    } else if (ctx.argc() == 2) {
      int h, m;
      if (!ctx.arguments(2, &h, &m)) return;
      ret.set(obj->as<Date>()->setHours(h, m));
    } else {
      int value;
      if (!ctx.arguments(1, &value)) return;
      ret.set(obj->as<Date>()->setHours(value));
    }
  });

  method("setMilliseconds", [](Context &ctx, Object *obj, Value &ret) {
    double value;
    if (!ctx.arguments(1, &value)) return;
    ret.set(obj->as<Date>()->setMilliseconds(value));
  });

  method("setMinutes", [](Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() >= 3) {
      int m, s, ms;
      if (!ctx.arguments(3, &m, &s, &ms)) return;
      ret.set(obj->as<Date>()->setMinutes(m, s, ms));
    } else if (ctx.argc() == 2) {
      int m, s;
      if (!ctx.arguments(2, &m, &s)) return;
      ret.set(obj->as<Date>()->setMinutes(m, s));
    } else {
      int value;
      if (!ctx.arguments(1, &value)) return;
      ret.set(obj->as<Date>()->setMinutes(value));
    }
  });

  method("setMonth", [](Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() >= 2) {
      int m, d;
      if (!ctx.arguments(2, &m, &d)) return;
      ret.set(obj->as<Date>()->setMonth(m, d));
    } else {
      int value;
      if (!ctx.arguments(1, &value)) return;
      ret.set(obj->as<Date>()->setMonth(value));
    }
  });

  method("setSeconds", [](Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() >= 2) {
      int s, ms;
      if (!ctx.arguments(2, &s, &ms)) return;
      ret.set(obj->as<Date>()->setSeconds(s, ms));
    } else {
      int value;
      if (!ctx.arguments(1, &value)) return;
      ret.set(obj->as<Date>()->setSeconds(value));
    }
  });

  method("setTime", [](Context &ctx, Object *obj, Value &ret) {
    double value;
    if (!ctx.arguments(1, &value)) return;
    ret.set(obj->as<Date>()->setTime(value));
  });

  method("toDateString", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(std::move(obj->as<Date>()->toDateString()));
  });

  method("toTimeString", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(std::move(obj->as<Date>()->toTimeString()));
  });

  method("toISOString", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(std::move(obj->as<Date>()->toISOString()));
  });

  method("toUTCString", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(std::move(obj->as<Date>()->toUTCString()));
  });
}

template<> void ClassDef<Constructor<Date>>::init() {
  super<Function>();
  ctor();
  method("now", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(Date::now());
  });
}

} // namespace pjs
