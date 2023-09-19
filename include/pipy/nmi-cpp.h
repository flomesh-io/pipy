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

#ifndef __PIPY_NMI_CPP_H__
#define __PIPY_NMI_CPP_H__

#include <pipy/nmi.h>

#include <string>

namespace pipy {
namespace nmi {

class Local;
class String;
class Object;
class Array;
class Global;

class Local {
public:
  enum Type {
    UNDEFINED = PJS_TYPE_UNDEFINED,
    BOOLEAN = PJS_TYPE_BOOLEAN,
    NUMBER = PJS_TYPE_NUMBER,
    STRING = PJS_TYPE_STRING,
    OBJECT = PJS_TYPE_OBJECT,
  };

  Local() : Local(pjs_undefined()) {}
  Local(const pjs_value value) : m_id(value) {}
  Local(bool b) : Local(pjs_boolean(b)) {}
  Local(double n) : Local(pjs_number(n)) {}
  Local(const Local &v) : Local(pjs_copy(pjs_undefined(), v.m_id)) {}

  static Local null() { return Local(pjs_null()); }

  auto id() const -> pjs_value { return m_id; }
  auto type() const -> Type { return (Type)pjs_type_of(m_id); }
  auto class_id() const -> int { return pjs_class_of(m_id); }
  bool is_undefined() const { return pjs_is_undefined(m_id); }
  bool is_null() const { return pjs_is_null(m_id); }
  bool is_nullish() const { return pjs_is_nullish(m_id); }
  bool is_empty_string() const { return pjs_is_empty_string(m_id); }
  bool is_instance_of(int class_id) const { return pjs_is_instance_of(m_id, class_id); }
  bool is_array() const { return pjs_is_array(m_id); }
  bool is_function() const { return pjs_is_function(m_id); }
  bool is_native() const { return pjs_is_native(m_id); }
  bool is_equal_to(const Local &rv) const { return pjs_is_equal(m_id, rv.m_id); }
  bool is_identical_to(const Local &rv) const { return pjs_is_identical(m_id, rv.m_id); }
  auto to_boolean() const -> bool { return pjs_to_boolean(m_id); }
  auto to_number() const -> double { return pjs_to_number(m_id); }
  auto to_string() const -> String;
  auto as_string() const -> String;
  auto as_object() const -> Object;
  auto as_array() const -> Array;

  auto operator=(const Local &v) -> Local& { pjs_copy(m_id, v.m_id); return *this; }
  bool operator==(const Local &v) const { return is_equal_to(v.m_id); }

protected:
  pjs_value m_id;
};

class String : public Local {
public:
  String(const char *s) : String(pjs_string(s, -1)) {}
  String(const char *s, size_t len) : String(pjs_string(s, len)) {}
  String(const std::string &s) : String(pjs_string(s.c_str(), s.length())) {}

  auto length() const -> size_t { return pjs_string_get_length(m_id); }
  auto utf8_size() const -> size_t { return pjs_string_get_utf8_size(m_id); }
  auto utf8_data() const -> std::string;
  auto utf8_data(char *buf, size_t len) -> size_t { return pjs_string_get_utf8_data(m_id, buf, len); }
  auto char_code_at(int pos) -> int { return pjs_string_get_char_code(m_id, pos); }

protected:
  String(const pjs_value value)
    : Local(value) {}

  friend class Local;
};

inline auto Local::to_string() const -> String {
  return String(pjs_to_string(m_id));
}

inline auto Local::as_string() const -> String {
  return String(m_id);
}

class Object : public Local {
public:
  Object() : Object(pjs_object()) {}

protected:
  Object(const pjs_value value)
    : Local(value) {}

  friend class Local;
};

inline auto Local::as_object() const -> Object {
  return Object(m_id);
}

class Array : public Local {
public:
  Array(size_t len = 0) : Array(pjs_array(len)) {}

protected:
  Array(const pjs_value value)
    : Local(value) {}

  friend class Local;
};

inline auto Local::as_array() const -> Array {
  return Array(m_id);
}

class Global : public Local {
public:
  Global(const Local &value) : Local(value.id()) {
    pjs_hold(m_id);
  }

  ~Global() {
    pjs_free(m_id);
  }
};

class PipelineBase {
public:
  PipelineBase(pipy_pipeline id) : m_id(id) {}

  auto id() const -> pipy_pipeline { return m_id; }
  void hold() { pipy_hold(m_id); }
  void free() { pipy_free(m_id); }
  void output(Local evt) { pipy_output_event(m_id, evt.id()); }

private:
  pipy_pipeline m_id;
};

class Variable {
public:
  void define(const char *name, const char *ns, Local value) {
    m_id = pipy_define_variable(-1, name, ns, value.id());
  }

  auto get(const PipelineBase *ppl) -> Local {
    Local val;
    pipy_get_variable(ppl->id(), m_id, val.id());
    return val;
  }

  void set(const PipelineBase *ppl, Local val) {
    pipy_set_variable(ppl->id(), m_id, val.id());
  }

private:
  int m_id = -1;
};

template<class T>
class PipelineTemplate {
public:
  static void define(const char *name = "") {
    pipy_define_pipeline(name, init, free, process);
  }

private:
  static void init(pipy_pipeline ppl, void **user_ptr) {
    *user_ptr = new T(ppl);
  }

  static void free(pipy_pipeline ppl, void *user_ptr) {
    delete static_cast<T*>(user_ptr);
  }

  static void process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
    static_cast<T*>(user_ptr)->process(Local(evt));
  }
};


} // namespace nmi
} // namespace pipy

#endif // __PIPY_NMI_CPP_H__
