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

bool Options::get_seconds(const pjs::Value &value, double &t) {
  if (value.is_number() && !std::isnan(value.n())) {
    t = value.n();
    return true;
  }
  if (value.is_string()) {
    auto n = utils::get_seconds(value.s()->str());
    if (!std::isnan(n)) {
      t = n;
      return true;
    }
  }
  return false;
}

Options::Value::Value(pjs::Object *options, const char *name, const char *base_name)
  : m_name(name)
  , m_base_name(base_name ? base_name : "options")
{
  if (options) {
    options->get(name, m_value);
  }
}

Options::Value::Value(pjs::Object *options, pjs::Str *name, const char *base_name)
  : m_name(name->c_str())
  , m_base_name(base_name ? base_name : "options")
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
  if (auto *s = get_string()) {
    value = s->str();
  }
  return *this;
}

Options::Value& Options::Value::get(pjs::Ref<pjs::Str> &value) {
  if (auto *s = get_string()) {
    value = s;
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
  if (Options::get_seconds(m_value, value)) {
    m_got = true;
    return *this;
  }
  return *this;
}

void Options::Value::check() {
  if (!m_got) {
    std::string msg(m_base_name);
    msg += '.';
    msg += m_name;
    msg += " expects ";
    bool first = true;
    for (size_t i = 0; i < m_type_count; i++) {
      if (first) first = false; else msg += " or ";
      switch (m_types[i]) {
        case BOOLEAN: msg += "a boolean"; break;
        case NUMBER: msg += "a number"; break;
        case FINITE_NUMBER: msg += "a finite number"; break;
        case POSITIVE_NUMBER: msg += "a positive number"; break;
        case STRING: msg += "a string"; break;
        case FUNCTION: msg += "a function"; break;
      }
    }
    for (size_t i = 0; i < m_class_count; i++) {
      if (first) first = false; else msg += " or ";
      msg += "a ";
      msg += m_classes[i]->name()->str();
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

auto Options::Value::get_string() -> pjs::Str* {
  add_type(STRING);
  if (m_got) return nullptr;
  if (m_value.is_nullish()) return nullptr;
  if (m_value.is_string()) {
    m_got = true;
    return m_value.s();
  }
  return nullptr;
}

auto Options::Value::get_object(pjs::Class *clazz) -> pjs::Object* {
  add_class(clazz);
  if (m_got) return nullptr;
  if (m_value.is_nullish()) return nullptr;
  if (m_value.is_instance_of(clazz)) {
    m_got = true;
    return m_value.o();
  }
  return nullptr;
}

void Options::Value::add_type(Type type) {
  if (m_type_count < sizeof(m_types) / sizeof(Type)) {
    m_types[m_type_count++] = type;
  }
}

void Options::Value::add_class(pjs::Class *clazz) {
  if (m_class_count < sizeof(m_classes) / sizeof(pjs::Class *)) {
    m_classes[m_class_count++] = clazz;
  }
}

void Options::Value::invalid_enum(const std::vector<pjs::Str*> &names) {
  std::string list;
  for (const auto &s : names) {
    if (!list.empty()) list += ", ";
    list += s->str();
  }
  std::string msg("options.");
  msg += m_name;
  msg += " expects one of ";
  msg += list;
  throw std::runtime_error(msg);
}

} // namespace pipy
