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
#include <cstdlib>
#include <limits>
#include <math.h>
#include <sstream>
#include <stack>

namespace pjs {

//
// Math
//

double Math::abs(double x) {
  return std::abs(x);
}

double Math::acos(double x) {
  return std::acos(x);
}

double Math::acosh(double x) {
  return std::acosh(x);
}

double Math::asin(double x) {
  return std::asin(x);
}

double Math::asinh(double x) {
  return std::asinh(x);
}

double Math::atan(double x) {
  return std::atan(x);
}

double Math::atanh(double x) {
  return std::atanh(x);
}

double Math::atan2(double y, double x) {
  return std::atan2(y, x);
}

double Math::cbrt(double x) {
  return std::cbrt(x);
}

double Math::ceil(double x) {
  return std::ceil(x);
}

double Math::cos(double x) {
  return std::cos(x);
}

double Math::cosh(double x) {
  return std::cosh(x);
}

double Math::exp(double x) {
  return std::exp(x);
}

double Math::expm1(double x) {
  return std::expm1(x);
}

double Math::floor(double x) {
  return std::floor(x);
}

double Math::fround(double x) {
  return float(x);
}

double Math::hypot(const double *v, int n) {
  if (n == 0) return 0;
  if (n == 1) return v[0];
  if (n == 2) return std::hypot(v[0], v[1]);
  double sum = 0;
  for (int i = 0; i < n; i++) sum += v[0] * v[0];
  return std::sqrt(sum);
}

double Math::log(double x) {
  return std::log(x);
}

double Math::log1p(double x) {
  return std::log1p(x);
}

double Math::log10(double x) {
  return std::log10(x);
}

double Math::log2(double x) {
  return std::log2(x);
}

double Math::max(const double *v, int n) {
  double m = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < n; i++) m = std::max(m, v[i]);
  return m;
}

double Math::min(const double *v, int n) {
  double m = std::numeric_limits<double>::infinity();
  for (int i = 0; i < n; i++) m = std::min(m, v[i]);
  return m;
}

double Math::pow(double x, double y) {
  return std::pow(x, y);
}

double Math::random() {
  return double(std::rand()) / (double(RAND_MAX) + 1);
}

double Math::round(double x) {
  return std::round(x);
}

double Math::sign(double x) {
  return std::copysign(std::fpclassify(x) == FP_ZERO ? 0 : 1, x);
}

double Math::sin(double x) {
  return std::sin(x);
}

double Math::sqrt(double x) {
  return std::sqrt(x);
}

double Math::tan(double x) {
  return std::tan(x);
}

double Math::tanh(double x) {
  return std::tanh(x);
}

double Math::trunc(double x) {
  return std::trunc(x);
}

int Math::clz32(int x) {
  return __builtin_clz(x);
}

int Math::imul(int x, int y) {
  return x * y;
}

template<> void ClassDef<Math>::init() {
  ctor();

  variable("E", M_E);
  variable("LN10", M_LN10);
  variable("LN2", M_LN2);
  variable("LOG10E", M_LOG10E);
  variable("LOG2E", M_LOG2E);
  variable("PI", M_PI);
  variable("SQRT1_2", M_SQRT1_2);
  variable("SQRT2", M_SQRT2);

  method("abs", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::abs(x.to_number()));
  });

  method("acos", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::acos(x.to_number()));
  });

  method("acosh", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::acosh(x.to_number()));
  });

  method("asin", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::asin(x.to_number()));
  });

  method("asinh", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::asinh(x.to_number()));
  });

  method("atan", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::atan(x.to_number()));
  });

  method("atanh", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::atanh(x.to_number()));
  });

  method("atan2", [](Context &ctx, Object *obj, Value &ret) {
    Value y, x;
    if (!ctx.arguments(2, &y, &x)) return;
    ret.set(Math::atan2(y.to_number(), x.to_number()));
  });

  method("cbrt", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::cbrt(x.to_number()));
  });

  method("ceil", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::ceil(x.to_number()));
  });

  method("cos", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::cos(x.to_number()));
  });

  method("cosh", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::cosh(x.to_number()));
  });

  method("exp", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::exp(x.to_number()));
  });

  method("expm1", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::expm1(x.to_number()));
  });

  method("floor", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::floor(x.to_number()));
  });

  method("fround", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::fround(x.to_number()));
  });

  method("hypot", [](Context &ctx, Object *obj, Value &ret) {
    int n = ctx.argc();
    double v[n];
    for (int i = 0; i < n; i++) v[i] = ctx.arg(i).to_number();
    ret.set(Math::hypot(v, n));
  });

  method("log", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::log(x.to_number()));
  });

  method("log1p", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::log1p(x.to_number()));
  });

  method("log10", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::log10(x.to_number()));
  });

  method("log2", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::log2(x.to_number()));
  });

  method("max", [](Context &ctx, Object *obj, Value &ret) {
    int n = ctx.argc();
    double v[n];
    for (int i = 0; i < n; i++) v[i] = ctx.arg(i).to_number();
    ret.set(Math::max(v, n));
  });

  method("min", [](Context &ctx, Object *obj, Value &ret) {
    int n = ctx.argc();
    double v[n];
    for (int i = 0; i < n; i++) v[i] = ctx.arg(i).to_number();
    ret.set(Math::min(v, n));
  });

  method("pow", [](Context &ctx, Object *obj, Value &ret) {
    Value x, y;
    if (!ctx.arguments(2, &x, &y)) return;
    ret.set(Math::pow(x.to_number(), y.to_number()));
  });

  method("random", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(Math::random());
  });

  method("round", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::round(x.to_number()));
  });

  method("sign", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::sign(x.to_number()));
  });

  method("sin", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::sin(x.to_number()));
  });

  method("sqrt", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::sqrt(x.to_number()));
  });

  method("tan", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::tan(x.to_number()));
  });

  method("tanh", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::tanh(x.to_number()));
  });

  method("trunc", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::trunc(x.to_number()));
  });

  method("clz32", [](Context &ctx, Object *obj, Value &ret) {
    Value x;
    if (!ctx.arguments(1, &x)) return;
    ret.set(Math::clz32(x.to_number()));
  });

  method("imul", [](Context &ctx, Object *obj, Value &ret) {
    Value x, y;
    if (!ctx.arguments(2, &x, &y)) return;
    ret.set(Math::imul(x.to_number(), y.to_number()));
  });
}

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
#ifdef _WIN32
   localtime_s(&m_tm,&t);
#else
  localtime_r(&t, &m_tm);
#endif
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
#ifdef _WIN32
   localtime_s(&m_tm,&t);
#else
  localtime_r(&t, &m_tm);
#endif
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
