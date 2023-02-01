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

template<class T>
auto Protobuf::Message::get_scalar(int field, WireType type) const -> typename T::T {
  auto r = get_tail_record(field, type);
  if (!r) return 0;
  return T(r->bits()).value();
}

template<class T>
auto Protobuf::Message::get_scalar_array(int field, WireType type) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field, type); r; r = r->next()) {
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
      pjs::Value v(T(r->bits()).value());
    }
  }
  return a;
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
  return get_scalar<Float>(field, WireType::I32);
}

auto Protobuf::Message::getDouble(int field) const -> double {
  return get_scalar<Double>(field, WireType::I64);
}

auto Protobuf::Message::getInt32(int field) const -> int32_t {
  return get_scalar<Int32>(field, WireType::VARINT);
}

auto Protobuf::Message::getInt64(int field) const -> int64_t {
  return get_scalar<Int64>(field, WireType::VARINT);
}

auto Protobuf::Message::getUint32(int field) const -> uint32_t {
  return get_scalar<Uint32>(field, WireType::VARINT);
}

auto Protobuf::Message::getUint64(int field) const -> uint64_t {
  return get_scalar<Uint64>(field, WireType::VARINT);
}

auto Protobuf::Message::getSint32(int field) const -> int32_t {
  return get_scalar<Sint32>(field, WireType::VARINT);
}

auto Protobuf::Message::getSint64(int field) const -> int64_t {
  return get_scalar<Sint64>(field, WireType::VARINT);
}

auto Protobuf::Message::getFixed32(int field) const -> int32_t {
  return get_scalar<Fixed32>(field, WireType::I32);
}

auto Protobuf::Message::getFixed64(int field) const -> int64_t {
  return get_scalar<Fixed64>(field, WireType::I64);
}

auto Protobuf::Message::getSfixed32(int field) const -> int32_t {
  return get_scalar<Sfixed32>(field, WireType::I32);
}

auto Protobuf::Message::getSfixed64(int field) const -> int64_t {
  return get_scalar<Sfixed64>(field, WireType::I64);
}

auto Protobuf::Message::getBool(int field) const -> bool {
  return get_scalar<Bool>(field, WireType::VARINT);
}

auto Protobuf::Message::getString(int field) const -> pjs::Str* {
  auto r = get_tail_record(field, WireType::LEN);
  if (!r) return nullptr;
  return pjs::Str::make(r->data().to_string())->retain();
}

auto Protobuf::Message::getBytes(int field) const -> const Data* {
  auto r = get_tail_record(field, WireType::LEN);
  if (!r) return nullptr;
  return &r->data();
}

auto Protobuf::Message::getMessage(int field) const -> Message* {
  auto r = get_tail_record(field, WireType::LEN);
  if (!r) return nullptr;
  auto *msg = Message::make();
  msg->retain();
  if (msg->deserialize(r->data())) return msg;
  msg->release();
  return nullptr;
}

auto Protobuf::Message::getFloatArray(int field) const -> pjs::Array* {
  return get_scalar_array<Float>(field, WireType::I32);
}

auto Protobuf::Message::getDoubleArray(int field) const -> pjs::Array* {
  return get_scalar_array<Double>(field, WireType::I64);
}

auto Protobuf::Message::getInt32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Int32>(field, WireType::VARINT);
}

auto Protobuf::Message::getInt64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Int64>(field, WireType::VARINT);
}

auto Protobuf::Message::getUint32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Uint32>(field, WireType::VARINT);
}

auto Protobuf::Message::getUint64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Uint64>(field, WireType::VARINT);
}

auto Protobuf::Message::getSint32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sint32>(field, WireType::VARINT);
}

auto Protobuf::Message::getSint64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sint64>(field, WireType::VARINT);
}

auto Protobuf::Message::getFixed32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Fixed32>(field, WireType::I32);
}

auto Protobuf::Message::getFixed64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Fixed64>(field, WireType::I64);
}

auto Protobuf::Message::getSfixed32Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sfixed32>(field, WireType::I32);
}

auto Protobuf::Message::getSfixed64Array(int field) const -> pjs::Array* {
  return get_scalar_array<Sfixed64>(field, WireType::I64);
}

auto Protobuf::Message::getBoolArray(int field) const -> pjs::Array* {
  return get_scalar_array<Bool>(field, WireType::VARINT);
}

auto Protobuf::Message::getStringArray(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field, WireType::LEN); r; r = r->next()) {
    a->push(pjs::Str::make(r->data().to_string()));
  }
  return a;
}

auto Protobuf::Message::getBytesArray(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field, WireType::LEN); r; r = r->next()) {
    a->push(Data::make(r->data()));
  }
  return a;
}

auto Protobuf::Message::getMessageArray(int field) const -> pjs::Array* {
  auto *a = pjs::Array::make();
  for (auto *r = get_all_records(field, WireType::LEN); r; r = r->next()) {
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

auto Protobuf::Message::get_all_records(int field, WireType type) const -> Record* {
  auto i = m_records.find(field);
  if (i == m_records.end()) return nullptr;
  auto r = i->second.head();
  if (r->type() == type || r->type() == WireType::LEN) return r;
  return nullptr;
}

auto Protobuf::Message::get_tail_record(int field, WireType type) const -> Record* {
  auto i = m_records.find(field);
  if (i == m_records.end()) return nullptr;
  auto r = i->second.tail();
  if (r->type() == type) return r;
  return nullptr;
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
    n |= (c & 0x7f) << (i * 7);
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

  method("getFixed32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getFixed32(field));
  });

  method("getFixed64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getFixed64(field));
  });

  method("getSfixed32", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSfixed32(field));
  });

  method("getSfixed64", [](Context &ctx, Object *obj, Value &ret) {
    int field;
    if (!ctx.arguments(1, &field)) return;
    ret.set(obj->as<Protobuf::Message>()->getSfixed64(field));
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
}

template<> void ClassDef<Constructor<Protobuf::Message>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
