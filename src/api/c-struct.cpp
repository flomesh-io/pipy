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

#include "api/c-struct.hpp"

namespace pipy {

thread_local static Data::Producer s_dp("CStruct");

//
// CStruct::Options
//

CStruct::Options::Options(pjs::Object *options) {
  Value(options, "isUnion")
    .get(is_union)
    .check_nullable();
}

//
// CStruct
//

void CStruct::field(const char *type, pjs::Str *name) {
  Field f;
  f.name = name;

  const char *p = std::strchr(type, '[');
  size_t n = 0;
  if (p) {
    n = p - type;
    char *end_p;
    auto count = std::strtol(p + 1, &end_p, 10);
    if (count < 0) throw std::runtime_error("negative array length");
    if (end_p[0] != ']') throw std::runtime_error("closing bracket expected");
    if (end_p[1]) throw std::runtime_error("redundant characters after bracket");
    f.count = count;
    f.is_array = true;
  } else {
    n = std::strlen(type);
    f.count = 1;
    f.is_array = false;
  }

  if (n == 4 && !std::strncmp("int8", type, n)) {
    f.size = 1;
    f.is_integral = true;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Number;
  } else if (n == 5 && !std::strncmp("int16", type, n)) {
    f.size = 2;
    f.is_integral = true;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Number;
  } else if (n == 5 && !std::strncmp("int32", type, n)) {
    f.size = 4;
    f.is_integral = true;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Number;
  } else if (n == 5 && !std::strncmp("int64", type, n)) {
    f.size = 8;
    f.is_integral = true;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Number;
  } else if (n == 5 && !std::strncmp("uint8", type, n)) {
    f.size = 1;
    f.is_integral = true;
    f.is_unsigned = true;
    f.type = pjs::Value::Type::Number;
  } else if (n == 6 && !std::strncmp("uint16", type, n)) {
    f.size = 2;
    f.is_integral = true;
    f.is_unsigned = true;
    f.type = pjs::Value::Type::Number;
  } else if (n == 6 && !std::strncmp("uint32", type, n)) {
    f.size = 4;
    f.is_integral = true;
    f.is_unsigned = true;
    f.type = pjs::Value::Type::Number;
  } else if (n == 6 && !std::strncmp("uint64", type, n)) {
    f.size = 8;
    f.is_integral = true;
    f.is_unsigned = true;
    f.type = pjs::Value::Type::Number;
  } else if (n == 5 && !std::strncmp("float", type, n)) {
    f.size = 4;
    f.is_integral = false;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Number;
  } else if (n == 6 && !std::strncmp("double", type, n)) {
    f.size = 8;
    f.is_integral = false;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Number;
  } else if (n == 4 && !std::strncmp("char", type, n)) {
    f.size = 1;
    f.is_integral = false;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::String;
  } else {
    throw std::runtime_error("unknown type name");
  }

  if (m_options.is_union) {
    f.offset = 0;
    m_size = std::max(m_size, f.size * f.count);
  } else {
    f.offset = align(m_size, align_size(f.size));
    m_size = f.offset + f.size * f.count;
  }

  m_fields.push_back(f);
}

void CStruct::field(CStruct *type, pjs::Str *name) {
  if (name) {
    Field f;
    f.offset = align(m_size, align_size(type->m_size));
    f.size = type->m_size;
    f.count = 1;
    f.is_array = false;
    f.is_integral = false;
    f.is_unsigned = false;
    f.type = pjs::Value::Type::Object;
    f.layout = type;
    f.name = name;
    m_fields.push_back(f);
    m_size = f.offset + align(f.size, 4);
  } else {
    if (!type->m_options.is_union) throw std::runtime_error("struct field name expected");
    auto offset = align(m_size, align_size(type->m_size));
    for (const auto &f : type->m_fields) {
      m_fields.push_back(f);
      m_fields.back().offset = offset;
    }
  }
}

auto CStruct::encode(pjs::Object *obj) -> Data* {
  Data buf;
  Data::Builder db(buf, &s_dp);
  encode(db, obj, this);
  db.flush();
  return Data::make(std::move(buf));
}

auto CStruct::decode(const Data &data) -> pjs::Object* {
  Data::Reader dr(data);
  return decode(dr, this);
}

auto CStruct::align(size_t offset, size_t alignment) -> size_t {
  return (offset + alignment - 1) / alignment * alignment;
}

auto CStruct::align_size(size_t size) -> size_t {
  if (size <= 1) return 1;
  if (size <= 2) return 2;
  if (size <= 4) return 4;
  return 8;
}

void CStruct::zero(Data::Builder &db, size_t count) {
  if (count > 0) {
    char zero[count];
    std::memset(zero, 0, count);
    db.push(zero, count);
  }
}

void CStruct::encode(Data::Builder &db, pjs::Object *values, CStruct *layout) {
  auto start = db.size();
  bool found = false;
  for (const auto &f : layout->m_fields) {
    pjs::Value v;
    values->get(f.name, v);
    if (layout->m_options.is_union && v.is_undefined()) continue; else found = true;
    auto offset = db.size() - start;
    if (offset < f.offset) zero(db, f.offset - offset);
    if (auto layout = f.layout.get()) {
      if (v.is_object() && v.o()) {
        encode(db, v.o(), layout);
      } else {
        zero(db, f.size * f.count);
      }
    } else if (f.type == pjs::Value::Type::String) {
      if (v.is_string()) {
        auto l = v.s()->size();
        auto n = f.size * f.count;
        char s[n];
        std::memset(s, 0, n);
        std::memcpy(s, v.s()->c_str(), std::min(n, l));
        db.push(s, n);
      } else {
        zero(db, f.size * f.count);
      }
    } else if (f.is_array) {
      if (v.is_array()) {
        auto a = v.as<pjs::Array>();
        for (int i = 0; i < f.count; i++) {
          a->get(i, v);
          encode(db, f.size, f.is_integral, f.is_unsigned, v);
        }
      } else {
        zero(db, f.size * f.count);
      }
    } else {
      encode(db, f.size, f.is_integral, f.is_unsigned, v);
    }
  }
  if (layout->m_options.is_union && !found) {
    zero(db, layout->m_size);
  }
}

void CStruct::encode(Data::Builder &db, int size, bool is_integral, bool is_unsigned, const pjs::Value &value) {
  char buf[8];
  if (!is_integral) {
    if (size == 4) {
      *(float*)buf = value.to_number();
    } else {
      *(double*)buf = value.to_number();
    }
  } else if (is_unsigned) {
    switch (size) {
      case 1: *(int8_t*)buf = value.to_int32(); break;
      case 2: *(int16_t*)buf = value.to_int32(); break;
      case 4: *(int32_t*)buf = value.to_int32(); break;
      case 8: *(int64_t*)buf = value.to_int64(); break;
    }
  } else {
    switch (size) {
      case 1: *(uint8_t*)buf = value.to_int32(); break;
      case 2: *(uint16_t*)buf = value.to_int32(); break;
      case 4: *(uint32_t*)buf = value.to_int32(); break;
      case 8: *(uint64_t*)buf = value.to_int64(); break;
    }
  }
  db.push(buf, size);
}

auto CStruct::decode(Data::Reader &dr, CStruct *layout) -> pjs::Object* {
  auto values = pjs::Object::make();
  if (layout->m_options.is_union) {
    Data buf;
    dr.read(layout->m_size, buf);
    for (const auto &f : layout->m_fields) {
      Data::Reader dr(buf);
      decode(dr, f, values);
    }
  } else {
    auto start = dr.position();
    for (const auto &f : layout->m_fields) {
      auto offset = dr.position() - start;
      if (offset < f.offset) dr.skip(f.offset - offset);
      decode(dr, f, values);
    }
  }
  return values;
}

void CStruct::decode(Data::Reader &dr, const Field &field, pjs::Object *values) {
  if (auto layout = field.layout.get()) {
    values->set(field.name, decode(dr, layout));
  } else if (field.type == pjs::Value::Type::String) {
    auto n = field.size * field.count;
    char s[n + 1];
    s[dr.read(n, s)] = 0;
    values->set(field.name, pjs::Str::make(s));
  } else if (field.is_array) {
    auto a = pjs::Array::make(field.count);
    for (int i = 0; i < field.count; i++) {
      pjs::Value v;
      decode(dr, field.size, field.is_integral, field.is_unsigned, v);
      a->set(i, v);
    }
    values->set(field.name, a);
  } else {
    pjs::Value v;
    decode(dr, field.size, field.is_integral, field.is_unsigned, v);
    values->set(field.name, v);
  }
}

void CStruct::decode(Data::Reader &dr, int size, bool is_integral, bool is_unsigned, pjs::Value &value) {
  char buf[size];
  auto len = dr.read(size, buf);
  if (len < size) std::memset(buf + len, 0, size - len);
  if (!is_integral) {
    if (size == 4) {
      value.set(*(float*)buf);
    } else {
      value.set(*(double*)buf);
    }
  } else if (is_unsigned) {
    switch (size) {
      case 1: value.set(*(uint8_t*)buf); break;
      case 2: value.set(*(uint16_t*)buf); break;
      case 4: value.set(*(uint32_t*)buf); break;
      case 8: value.set(*(uint64_t*)buf); break;
    }
  } else {
    switch (size) {
      case 1: value.set(*(int8_t*)buf); break;
      case 2: value.set(*(int16_t*)buf); break;
      case 4: value.set(*(int32_t*)buf); break;
      case 8: value.set(*(int64_t*)buf); break;
    }
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<CStruct>::init() {
  ctor([](Context &ctx) -> Object* {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return nullptr;
    return CStruct::make(options);
  });

  accessor("size", [](Object *obj, Value &ret) {
    ret.set((int)obj->as<CStruct>()->size());
  });

  method("field", [](Context &ctx, Object *obj, Value &ret) {
    Str *type, *name = nullptr;
    CStruct *c_struct;
    try {
      if (ctx.get(0, c_struct) && c_struct) {
        if (!ctx.arguments(1, &c_struct, &name)) return;
        obj->as<CStruct>()->field(c_struct, name);
      } else {
        if (!ctx.arguments(2, &type, &name)) return;
        obj->as<CStruct>()->field(type->c_str(), name);
      }
      ret.set(obj);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Object *values;
    if (!ctx.arguments(1, &values)) return;
    if (!values) { ret = Value::null; return; }
    ret.set(obj->as<CStruct>()->encode(values));
  });

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    ret.set(obj->as<CStruct>()->decode(*data));
  });
}

template<> void ClassDef<Constructor<CStruct>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
