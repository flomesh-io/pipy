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

#include "protobuf.hpp"

#include <limits>

namespace pipy {

static Data::Producer s_dp("Protobuf");

template<class T>
auto Protobuf::Message::get_scalar(int field) const -> typename T::T {
  auto r = get_tail_record(field);
  if (!r) return 0;
  return T(r).value();
}

template<class T>
auto Protobuf::Message::get_scalar_array(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field); r; r = r->next()) {
    if (r->type() == WireType::LEN) {
      Data::Reader dr(r->data());
      while (!dr.eof()) {
        T value;
        if (!value.read(dr)) {
          a->retain();
          a->release();
          return nullptr;
        }
        pjs::Value v(value.value());
        a->push(v);
      }
    } else {
      pjs::Value v(T(r).value());
    }
  }
  return a;
}

template<class T>
void Protobuf::Message::set_scalar(int field, WireType type, typename T::T value) {
  T v(value);
  set_record(field, new Record(field, type, v.bits));
}

template<class T>
void Protobuf::Message::set_scalar_array(int field, pjs::Array *values) {
  Data buf;
  Data::Builder db(buf, &s_dp);
  values->iterate_all(
    [&](pjs::Value &v, int) {
      typename T::T value = 0;
      if (v.is_number()) value = (typename T::T)v.n();
      T(value).write(db);
    }
  );
  db.flush();
  set_record(field, new Record(field, WireType::LEN, buf));
}

Protobuf::Message::~Message() {
  for (auto &p : m_records) {
    auto *r = p.second.head();
    while (r) {
      auto rec = r; r = r->next();
      delete rec;
    }
  }
}

auto Protobuf::Message::getWireType(int field) const -> WireType {
  auto i = m_records.find(field);
  if (i == m_records.end()) return WireType::NONE;
  return i->second.tail()->type();
}

auto Protobuf::Message::getFloat(int field) const -> float {
  return get_scalar<Float>(field);
}

auto Protobuf::Message::getDouble(int field) const -> double {
  return get_scalar<Double>(field);
}

auto Protobuf::Message::getInt32(int field) const -> int32_t {
  return get_scalar<Int32>(field);
}

auto Protobuf::Message::getInt64(int field) const -> int64_t {
  return get_scalar<Int64>(field);
}

auto Protobuf::Message::getUint32(int field) const -> uint32_t {
  return get_scalar<Uint32>(field);
}

auto Protobuf::Message::getUint64(int field) const -> uint64_t {
  return get_scalar<Uint64>(field);
}

auto Protobuf::Message::getSint32(int field) const -> int32_t {
  return get_scalar<Sint32>(field);
}

auto Protobuf::Message::getSint64(int field) const -> int64_t {
  return get_scalar<Sint64>(field);
}

auto Protobuf::Message::getBool(int field) const -> bool {
  return get_scalar<Bool>(field);
}

auto Protobuf::Message::getString(int field) const -> pjs::Str* {
  auto r = get_tail_record(field);
  if (!r || r->type() != WireType::LEN) return nullptr;
  return pjs::Str::make(r->data().to_string())->retain();
}

auto Protobuf::Message::getBytes(int field) const -> const Data* {
  auto r = get_tail_record(field);
  if (!r || r->type() != WireType::LEN) return nullptr;
  return &r->data();
}

auto Protobuf::Message::getMessage(int field) const -> Message* {
  auto r = get_tail_record(field);
  if (!r || r->type() != WireType::LEN) return nullptr;
  auto *msg = Message::make();
  msg->retain();
  if (msg->deserialize(r->data())) return msg;
  msg->release();
  return nullptr;
}

auto Protobuf::Message::getFloatArray(int field) const -> pjs::Array* {
  return get_scalar_array<Float>(field);
}

auto Protobuf::Message::getDoubleArray(int field) const -> pjs::Array* {
  return get_scalar_array<Double>(field);
}

auto Protobuf::Message::getInt32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Int32>(field);
}

auto Protobuf::Message::getInt64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Int64>(field);
}

auto Protobuf::Message::getUint32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Uint32>(field);
}

auto Protobuf::Message::getUint64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Uint64>(field);
}

auto Protobuf::Message::getSint32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sint32>(field);
}

auto Protobuf::Message::getSint64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sint64>(field);
}

auto Protobuf::Message::getFixed32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Fixed32>(field);
}

auto Protobuf::Message::getFixed64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Fixed64>(field);
}

auto Protobuf::Message::getSfixed32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sfixed32>(field);
}

auto Protobuf::Message::getSfixed64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sfixed64>(field);
}

auto Protobuf::Message::getBoolArray(int field) const -> pjs::Array* {
  return get_scalar_array<Bool>(field);
}

auto Protobuf::Message::getStringArray(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field); r; r = r->next()) {
    a->push(pjs::Str::make(r->data().to_string()));
  }
  return a;
}

auto Protobuf::Message::getBytesArray(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field); r; r = r->next()) {
    a->push(Data::make(r->data()));
  }
  return a;
}

auto Protobuf::Message::getMessageArray(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field); r; r = r->next()) {
    auto *msg = Message::make();
    a->push(msg);
    if (!msg->deserialize(r->data())) {
      a->retain();
      a->release();
      return nullptr;
    }
  }
  return a;
}

void Protobuf::Message::setFloat(int field, float value) {
  set_scalar<Float>(field, WireType::I32, value);
}

void Protobuf::Message::setDouble(int field, double value) {
  set_scalar<Double>(field, WireType::I64, value);
}

void Protobuf::Message::setInt32(int field, int32_t value) {
  set_scalar<Int32>(field, WireType::VARINT, value);
}

void Protobuf::Message::setInt64(int field, int64_t value) {
  set_scalar<Int64>(field, WireType::VARINT, value);
}

void Protobuf::Message::setUint32(int field, uint32_t value) {
  set_scalar<Uint32>(field, WireType::VARINT, value);
}

void Protobuf::Message::setUint64(int field, uint64_t value) {
  set_scalar<Uint64>(field, WireType::VARINT, value);
}

void Protobuf::Message::setSint32(int field, int32_t value) {
  set_scalar<Sint32>(field, WireType::VARINT, value);
}

void Protobuf::Message::setSint64(int field, int64_t value) {
  set_scalar<Sint64>(field, WireType::VARINT, value);
}

void Protobuf::Message::setFixed32(int field, int32_t value) {
  set_scalar<Fixed32>(field, WireType::I32, value);
}

void Protobuf::Message::setFixed64(int field, int64_t value) {
  set_scalar<Fixed64>(field, WireType::I64, value);
}

void Protobuf::Message::setSfixed32(int field, int32_t value) {
  set_scalar<Sfixed32>(field, WireType::I32, value);
}

void Protobuf::Message::setSfixed64(int field, int64_t value) {
  set_scalar<Sfixed64>(field, WireType::I64, value);
}

void Protobuf::Message::setBool(int field, bool value) {
  set_scalar<Bool>(field, WireType::VARINT, value);
}

void Protobuf::Message::setString(int field, pjs::Str *value) {
  Data buf(value->c_str(), value->size(), &s_dp);
  set_record(field, new Record(field, WireType::LEN, buf));
}

void Protobuf::Message::setBytes(int field, const Data &value) {
  set_record(field, new Record(field, WireType::LEN, value));
}

void Protobuf::Message::setMessage(int field, Message *value) {
  Data buf;
  value->serialize(buf);
  set_record(field, new Record(field, WireType::LEN, buf));
}

void Protobuf::Message::setFloatArray(int field, pjs::Array *values) {
  set_scalar_array<Float>(field, values);
}

void Protobuf::Message::setDoubleArray(int field, pjs::Array *values) {
  set_scalar_array<Double>(field, values);
}

void Protobuf::Message::setInt32Array(int field, pjs::Array *values) {
  set_scalar_array<Int32>(field, values);
}

void Protobuf::Message::setInt64Array(int field, pjs::Array *values) {
  set_scalar_array<Int64>(field, values);
}

void Protobuf::Message::setUint32Array(int field, pjs::Array *values) {
  set_scalar_array<Uint32>(field, values);
}

void Protobuf::Message::setUint64Array(int field, pjs::Array *values) {
  set_scalar_array<Uint64>(field, values);
}

void Protobuf::Message::setSint32Array(int field, pjs::Array *values) {
  set_scalar_array<Sint32>(field, values);
}

void Protobuf::Message::setSint64Array(int field, pjs::Array *values) {
  set_scalar_array<Sint64>(field, values);
}

void Protobuf::Message::setFixed32Array(int field, pjs::Array *values) {
  set_scalar_array<Fixed32>(field, values);
}

void Protobuf::Message::setFixed64Array(int field, pjs::Array *values) {
  set_scalar_array<Fixed64>(field, values);
}

void Protobuf::Message::setSfixed32Array(int field, pjs::Array *values) {
  set_scalar_array<Sfixed32>(field, values);
}

void Protobuf::Message::setSfixed64Array(int field, pjs::Array *values) {
  set_scalar_array<Sfixed64>(field, values);
}

void Protobuf::Message::setBoolArray(int field, pjs::Array *values) {
  set_scalar_array<Bool>(field, values);
}

void Protobuf::Message::setStringArray(int field, pjs::Array *values) {
}

void Protobuf::Message::setBytesArray(int field, pjs::Array *values) {
}

void Protobuf::Message::setMessageArray(int field, pjs::Array *values) {
}

void Protobuf::Message::serialize(Data &data) {
}

bool Protobuf::Message::deserialize(const Data &data) {
  Data::Reader r(data);
  while (!r.eof()) {
    auto rec = read_record(r);
    if (!rec) return false;
    auto &list = m_records[rec->index()];
    list.push(rec);
    if (list.head()->type() != rec->type()) return false;
  }
  return true;
}

auto Protobuf::Message::get_all_records(int field) const -> Record* {
  auto i = m_records.find(field);
  if (i == m_records.end()) return nullptr;
  return i->second.head();
}

auto Protobuf::Message::get_tail_record(int field) const -> Record* {
  auto i = m_records.find(field);
  if (i == m_records.end()) return nullptr;
  return i->second.tail();
}

void Protobuf::Message::set_record(int field, Record *rec) {
  auto &list = m_records[field];
  while (auto *r = list.head()) {
    list.remove(r);
    delete r;
  }
  list.push(rec);
}

auto Protobuf::Message::read_record(Data::Reader &r) -> Record* {
  uint64_t tag;
  if (!read_varint(r, tag)) return nullptr;
  auto index = tag >> 3;
  switch (tag & 7) {
    case 0: {
      uint64_t data;
      if (!read_varint(r, data)) return nullptr;
      return new Record(index, WireType::VARINT, data);
    }
    case 1: {
      uint64_t data;
      if (!read_uint64(r, data)) return nullptr;
      return new Record(index, WireType::I64, data);
    }
    case 2: {
      Data data;
      uint64_t length;
      if (!read_varint(r, length)) return nullptr;
      if (r.read(length, data) < length) return nullptr;
      return new Record(index, WireType::LEN, data);
    }
    case 5: {
      uint32_t data;
      if (!read_uint32(r, data)) return nullptr;
      return new Record(index, WireType::I32, data);
    }
    default: return nullptr;
  }
}

bool Protobuf::Message::read_varint(Data::Reader &r, uint64_t &n) {
  n = 0;
  for (int i = 0; i < 10; i++) {
    auto c = r.get();
    if (c < 0) return false;
    n |= (uint64_t)(c & 0x7f) << (i * 7);
    if (!(c & 0x80)) return true;
  }
  return false;
}

bool Protobuf::Message::read_varint(Data::Reader &r, uint32_t &n) {
  uint64_t data;
  if (!read_varint(r, data)) return false;
  n = (uint32_t)data;
  return true;
}

bool Protobuf::Message::read_uint32(Data::Reader &r, uint32_t &n) {
  uint8_t buf[4];
  if (r.read(4, buf) < 4) return false;
  n = (
    ((uint32_t)buf[3] << 24)|
    ((uint32_t)buf[2] << 16)|
    ((uint32_t)buf[1] <<  8)|
    ((uint32_t)buf[0]      )
  );
  return true;
}

bool Protobuf::Message::read_uint64(Data::Reader &r, uint64_t &n) {
  uint8_t buf[8];
  if (r.read(8, buf) < 8) return false;
  n = (
    ((uint64_t)buf[7] << 56)|
    ((uint64_t)buf[6] << 48)|
    ((uint64_t)buf[5] << 40)|
    ((uint64_t)buf[4] << 32)|
    ((uint64_t)buf[3] << 24)|
    ((uint64_t)buf[2] << 16)|
    ((uint64_t)buf[1] <<  8)|
    ((uint64_t)buf[0]      )
  );
  return true;
}

void Protobuf::Message::write_varint(Data::Builder &db, uint64_t n) {
  do {
    char c = n & 0x7f;
    if (n >>= 7) c |= 0x80;
    db.push(c);
  } while (n);
}

void Protobuf::Message::write_uint32(Data::Builder &db, uint32_t n) {
  db.push((uint8_t)(n >>  0));
  db.push((uint8_t)(n >>  8));
  db.push((uint8_t)(n >> 16));
  db.push((uint8_t)(n >> 24));
}

void Protobuf::Message::write_uint64(Data::Builder &db, uint64_t n) {
  db.push((uint8_t)(n >>  0));
  db.push((uint8_t)(n >>  8));
  db.push((uint8_t)(n >> 16));
  db.push((uint8_t)(n >> 24));
  db.push((uint8_t)(n >> 32));
  db.push((uint8_t)(n >> 40));
  db.push((uint8_t)(n >> 48));
  db.push((uint8_t)(n >> 56));
}

auto Protobuf::Message::decode_sint(uint32_t n) -> int32_t {
  return (n >> 1) ^ -(n & 1);
}

auto Protobuf::Message::decode_sint(uint64_t n) -> int64_t {
  return (n >> 1) ^ -(n & 1);
}

auto Protobuf::Message::encode_sint(int32_t n) -> uint32_t {
  return (n << 1) ^ (n >> 31);
}

auto Protobuf::Message::encode_sint(int64_t n) -> uint64_t {
  return (n << 1) ^ (n >> 63);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// Protobuf
//

template<> void ClassDef<Protobuf>::init() {
  ctor();
  variable("Message", class_of<Constructor<Protobuf::Message>>());
}

//
// Protobuf::WireType
//

template<> void EnumDef<Protobuf::WireType>::init() {
  define(Protobuf::WireType::NONE, "");
  define(Protobuf::WireType::VARINT, "VARINT");
  define(Protobuf::WireType::I32, "I32");
  define(Protobuf::WireType::I64, "I64");
  define(Protobuf::WireType::LEN, "LEN");
}

//
// Protobuf::Message
//

template<> void ClassDef<Protobuf::Message>::init() {
  ctor([](Context &ctx) -> Object* {
    pipy::Data *data = nullptr;
    if (!ctx.arguments(0, &data)) return nullptr;
    auto *obj = Protobuf::Message::make();
    if (data) {
      if (!obj->deserialize(*data)) {
        obj->retain();
        obj->release();
        ctx.error("Protobuf deserializing error");
        return nullptr;
      }
    }
    return obj;
  });

  method("getWireType", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    auto type = obj->as<Protobuf::Message>()->getWireType(field);
    ret.set(EnumDef<Protobuf::WireType>::name(type));
  });

  method("getFloat", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getFloat(field));
  });

  method("getDouble", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getDouble(field));
  });

  method("getInt32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getInt32(field));
  });

  method("getInt64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getInt64(field));
  });

  method("getUint32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getUint32(field));
  });

  method("getUint64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getUint64(field));
  });

  method("getSint32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSint32(field));
  });

  method("getSint64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSint64(field));
  });

  method("getBool", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getBool(field));
  });

  method("getString", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getString(field));
  });

  method("getBytes", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getBytes(field));
  });

  method("getMessage", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getMessage(field));
  });

  method("getFloatArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getFloatArray(field));
  });

  method("getDoubleArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getDoubleArray(field));
  });

  method("getInt32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getInt32Array(field));
  });

  method("getInt64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getInt64Array(field));
  });

  method("getUint32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getUint32Array(field));
  });

  method("getUint64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getUint64Array(field));
  });

  method("getSint32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSint32Array(field));
  });

  method("getSint64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSint64Array(field));
  });

  method("getFixed32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getFixed32Array(field));
  });

  method("getFixed64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getFixed64Array(field));
  });

  method("getSfixed32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSfixed32Array(field));
  });

  method("getSfixed64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSfixed64Array(field));
  });

  method("getBoolArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getBoolArray(field));
  });

  method("getStringArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getStringArray(field));
  });

  method("getBytesArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getBytesArray(field));
  });

  method("getMessageArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getMessageArray(field));
  });

  method("setFloat", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setFloat(field, value);
    ret.set(obj);
  });

  method("setDouble", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setDouble(field, value);
    ret.set(obj);
  });

  method("setInt32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setInt32(field, value);
    ret.set(obj);
  });

  method("setInt64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setInt64(field, value);
    ret.set(obj);
  });

  method("setUint32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setUint32(field, value);
    ret.set(obj);
  });

  method("setUint64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setUint64(field, value);
    ret.set(obj);
  });

  method("setSint32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setSint32(field, value);
    ret.set(obj);
  });

  method("setSint64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setSint64(field, value);
    ret.set(obj);
  });

  method("setFixed32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setFixed32(field, value);
    ret.set(obj);
  });

  method("setFixed64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setFixed64(field, value);
    ret.set(obj);
  });

  method("setSfixed32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setSfixed32(field, value);
    ret.set(obj);
  });

  method("setSfixed64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    double value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setSfixed64(field, value);
    ret.set(obj);
  });

  method("setBool", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    bool value;
    if (!ctx.arguments(2, &field, &value)) return;
    obj->as<Protobuf::Message>()->setBool(field, value);
    ret.set(obj);
  });

  method("setFloatArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setFloatArray(field, values);
    ret.set(obj);
  });

  method("setDoubleArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setDoubleArray(field, values);
    ret.set(obj);
  });

  method("setInt32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setInt32Array(field, values);
    ret.set(obj);
  });

  method("setInt64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setInt64Array(field, values);
    ret.set(obj);
  });

  method("setUint32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setUint32Array(field, values);
    ret.set(obj);
  });

  method("setUint64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setUint64Array(field, values);
    ret.set(obj);
  });

  method("setSint32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setSint32Array(field, values);
    ret.set(obj);
  });

  method("setSint64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setSint64Array(field, values);
    ret.set(obj);
  });

  method("setFixed32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setFixed32Array(field, values);
    ret.set(obj);
  });

  method("setFixed64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setFixed64Array(field, values);
    ret.set(obj);
  });

  method("setSfixed32Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setSfixed32Array(field, values);
    ret.set(obj);
  });

  method("setSfixed64Array", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setSfixed64Array(field, values);
    ret.set(obj);
  });

  method("setBoolArray", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    Array *values;
    if (!ctx.arguments(2, &field, &values)) return;
    obj->as<Protobuf::Message>()->setBoolArray(field, values);
    ret.set(obj);
  });
}

template<> void ClassDef<Constructor<Protobuf::Message>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
