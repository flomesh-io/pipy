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

#ifndef PJS_BUILTIN_HPP
#define PJS_BUILTIN_HPP

#include "types.hpp"

#include <ctime>
#include <functional>

namespace pjs {

//
// Math
//

class Math : public ObjectTemplate<Math> {
public:
  static void init();

  static double abs(double x);
  static double acos(double x);
  static double acosh(double x);
  static double asin(double x);
  static double asinh(double x);
  static double atan(double x);
  static double atanh(double x);
  static double atan2(double y, double x);
  static double cbrt(double x);
  static double ceil(double x);
  static double cos(double x);
  static double cosh(double x);
  static double exp(double x);
  static double expm1(double x);
  static double floor(double x);
  static double fround(double x);
  static double hypot(const double *v, int n);
  static double log(double x);
  static double log1p(double x);
  static double log10(double x);
  static double log2(double x);
  static double max(const double *v, int n);
  static double min(const double *v, int n);
  static double pow(double x, double y);
  static double random();
  static double round(double x);
  static double sign(double x);
  static double sin(double x);
  static double sqrt(double x);
  static double tan(double x);
  static double tanh(double x);
  static double trunc(double x);

  static int clz32(int x);
  static int imul(int x, int y);
};

//
// Date
//

class Date : public ObjectTemplate<Date> {
public:
  static auto now() -> double;

  auto getDate() -> int { return m_tm.tm_mday; }
  auto getDay() -> int { return m_tm.tm_wday; }
  auto getFullYear() -> int { return m_tm.tm_year + 1900; }
  auto getHours() -> int { return m_tm.tm_hour; }
  auto getMilliseconds() -> int { return m_msec; }
  auto getMinutes() -> int { return m_tm.tm_min; }
  auto getMonth() -> int { return m_tm.tm_mon; }
  auto getSeconds() -> int { return m_tm.tm_sec; }
  auto getTime() -> double;

  auto setDate(int value) -> double;
  auto setFullYear(int value) -> double;
  auto setFullYear(int y, int m) -> double;
  auto setFullYear(int y, int m, int d) -> double;
  auto setHours(int value) -> double;
  auto setHours(int h, int m) -> double;
  auto setHours(int h, int m, int s) -> double;
  auto setHours(int h, int m, int s, int ms) -> double;
  auto setMilliseconds(int value) -> double;
  auto setMinutes(int value) -> double;
  auto setMinutes(int m, int s) -> double;
  auto setMinutes(int m, int s, int ms) -> double;
  auto setMonth(int value) -> double;
  auto setMonth(int m, int d) -> double;
  auto setSeconds(int value) -> double;
  auto setSeconds(int s, int ms) -> double;
  auto setTime(double value) -> double;

  auto toDateString() -> std::string;
  auto toTimeString() -> std::string;
  auto toISOString() -> std::string;
  auto toUTCString() -> std::string;

  virtual void value_of(Value &out) override;
  virtual auto to_string() const -> std::string override;
  virtual auto dump() -> Object* override;

private:
  Date();
  Date(const Date *date);
  Date(double value);
  Date(int year, int mon, int day = 1, int hour = 0, int min = 0, int sec = 0, int ms = 0);

  std::tm m_tm;
  int m_msec;

  auto normalize() -> double;

  friend class ObjectTemplate<Date>;
};

//
// Map
//

class Map : public ObjectTemplate<Map> {
public:
  auto size() -> size_t { return m_ht->size(); }
  void clear() { m_ht->clear(); }
  bool erase(const Value &key) { return m_ht->erase(key); }
  void get(const Value &key, Value &value) { m_ht->get(key, value); }
  void set(const Value &key, const Value &value) { m_ht->set(key, value); }
  bool has(const Value &key) { return m_ht->has(key); }

  void forEach(const std::function<bool(const Value &, const Value &)> &cb) {
    OrderedHash<Value, Value>::Iterator it(m_ht);
    while (auto *ent = it.next()) {
      if (!cb(ent->k, ent->v)) break;
    }
  }

private:
  Map() : m_ht(OrderedHash<Value, Value>::make()) {}

  Map(Array *entries) : m_ht(OrderedHash<Value, Value>::make()) {
    entries->iterate_all(
      [this](Value &p, int i) {
        if (!p.is_array()) {
          std::string msg("Entry expects an array at index ");
          throw std::runtime_error(msg + std::to_string(i));
        }
        Value k, v;
        p.as<Array>()->get(0, k);
        p.as<Array>()->get(1, v);
        set(k, v);
      }
    );
  }

  Ref<OrderedHash<Value, Value>> m_ht;

  friend class ObjectTemplate<Map>;
};

//
// Set
//

class Set : public ObjectTemplate<Set> {
public:
  auto size() -> size_t { return m_ht->size(); }
  void clear() { m_ht->clear(); }
  bool erase(const Value &value) { return m_ht->erase(value); }
  void add(const Value &value) { m_ht->set(value, true); }
  bool has(const Value &value) { return m_ht->has(value); }

  void forEach(const std::function<bool(const Value &)> &cb) {
    OrderedHash<Value, bool>::Iterator it(m_ht);
    while (auto *ent = it.next()) {
      if (!cb(ent->k)) break;
    }
  }

private:
  Set() : m_ht(OrderedHash<Value, bool>::make()) {}

  Set(Array *values) : m_ht(OrderedHash<Value, bool>::make()) {
    values->iterate_all(
      [this](Value &v, int) {
        add(v);
      }
    );
  }

  Ref<OrderedHash<Value, bool>> m_ht;

  friend class ObjectTemplate<Set>;
};

} // namespace pjs

#endif // PJS_BUILTIN_HPP
