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

#include "options.hpp"
#include "utils.hpp"

#include <stdexcept>

namespace pipy {

Options::Value::Value(pjs::Object *options, const char *name)
  : m_name(name)
{
  if (options) {
    options->get(name, m_value);
  }
}

Options::Value& Options::Value::get(bool &value) {
  add_type(BOOLEAN);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  if (m_value.is_boolean()) {
    value = m_value.b();
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get(double &value, int thousand) {
  add_type(NUMBER);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  if (get_number(value, thousand)) {
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get(int &value, int thousand) {
  add_type(FINITE_NUMBER);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  double v;
  if (get_number(v, thousand) && !std::isinf(v)) {
    value = v;
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get(size_t &value, int thousand) {
  add_type(POSITIVE_NUMBER);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  double v;
  if (get_number(v, thousand) && !std::isinf(v) && v >= 0) {
    value = v;
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get(std::string &value) {
  add_type(STRING);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  if (m_value.is_string()) {
    value = m_value.s()->str();
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get(pjs::Ref<pjs::Str> &value) {
  add_type(STRING);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  if (m_value.is_string()) {
    value = m_value.s();
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get(pjs::Ref<pjs::Function> &value) {
  add_type(FUNCTION);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  if (m_value.is_function()) {
    value = m_value.f();
    m_got = true;
    return *this;
  }
  return *this;
}

Options::Value& Options::Value::get_binary_size(int &value) {
  return get(value, 1024);
}

Options::Value& Options::Value::get_binary_size(size_t &value) {
  return get(value, 1024);
}

Options::Value& Options::Value::get_seconds(double &value) {
  add_type(NUMBER);
  if (m_got) return *this;
  if (m_value.is_nullish()) return *this;
  if (m_value.is_number() && !std::isnan(m_value.n())) {
    value = m_value.n();
    m_got = true;
    return *this;
  }
  if (m_value.is_string()) {
    auto n = utils::get_seconds(m_value.s()->str());
    if (!std::isnan(n)) {
      value = n;
      m_got = true;
      return *this;
    }
  }
  return *this;
}

void Options::Value::check() {
  if (!m_got) {
    std::string msg("options.");
    msg += m_name;
    msg += " expects ";
    for (size_t i = 0; i < m_type_count; i++) {
      if (i > 0) msg += " or ";
      switch (m_types[i]) {
        case BOOLEAN: msg += "a boolean"; break;
        case NUMBER: msg += "a number"; break;
        case FINITE_NUMBER: msg += "a finite number"; break;
        case POSITIVE_NUMBER: msg += "a positive number"; break;
        case STRING: msg += "a string"; break;
        case FUNCTION: msg += "a function"; break;
      }
    }
    throw std::runtime_error(msg);
  }
}

void Options::Value::check_nullable() {
  if (!m_value.is_nullish()) {
    check();
  }
}

bool Options::Value::get_number(double &value, int thousand) {
  if (m_value.is_number() && !std::isnan(m_value.n())) {
    value = m_value.n();
    return true;
  }
  if (m_value.is_string()) {
    auto n = utils::get_size(m_value.s()->str(), thousand);
    if (!std::isnan(n)) {
      value = n;
      return true;
    }
  }
  return false;
}

void Options::Value::add_type(Type type) {
  if (m_type_count < sizeof(m_types) / sizeof(Type)) {
    m_types[m_type_count++] = type;
  }
}

} // namespace pipy
