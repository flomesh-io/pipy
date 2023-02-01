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

#ifndef PROTOBUF_HPP
#define PROTOBUF_HPP

#include "data.hpp"

namespace pipy {
namespace protobuf {

//
// Message
//

class Message : public pjs::ObjectTemplate<Message> {
public:
  enum class WireType {
    NONE,
    VARINT,
    I32,
    I64,
    LEN,
  };

  ~Message();

  auto getWireType(int field) const -> WireType;
  auto getFloat(int field) const -> float;
  auto getDouble(int field) const -> double;
  auto getInt32(int field) const -> int32_t;
  auto getInt64(int field) const -> int64_t;
  auto getUint32(int field) const -> uint32_t;
  auto getUint64(int field) const -> uint64_t;
  auto getSint32(int field) const -> int32_t;
  auto getSint64(int field) const -> int64_t;
  auto getFixed32(int field) const -> int32_t;
  auto getFixed64(int field) const -> int64_t;
  auto getSfixed32(int field) const -> int32_t;
  auto getSfixed64(int field) const -> int64_t;
  auto getBool(int field) const -> bool;
  auto getString(int field) const -> pjs::Str*;
  auto getBytes(int field) const -> const Data*;
  auto getMessage(int field) const -> Message*;
  auto getFloatArray(int field) const -> pjs::Array*;
  auto getDoubleArray(int field) const -> pjs::Array*;
  auto getInt32Array(int field) const -> pjs::Array*;
  auto getInt64Array(int field) const -> pjs::Array*;
  auto getUint32Array(int field) const -> pjs::Array*;
  auto getUint64Array(int field) const -> pjs::Array*;
  auto getSint32Array(int field) const -> pjs::Array*;
  auto getSint64Array(int field) const -> pjs::Array*;
  auto getFixed32Array(int field) const -> pjs::Array*;
  auto getFixed64Array(int field) const -> pjs::Array*;
  auto getSfixed32Array(int field) const -> pjs::Array*;
  auto getSfixed64Array(int field) const -> pjs::Array*;
  auto getBoolArray(int field) const -> pjs::Array*;
  auto getStringArray(int field) const -> pjs::Array*;
  auto getBytesArray(int field) const -> pjs::Array*;
  auto getMessageArray(int field) const -> pjs::Array*;
  void setDouble(int field, double value);
  void setFloat(int field, float value);
  void setInt32(int field, int32_t value);
  void setInt64(int field, int64_t value);
  void setUint32(int field, uint32_t value);
  void setUint64(int field, uint64_t value);
  void setSint32(int field, int32_t value);
  void setSint64(int field, int64_t value);
  void setFixed32(int field, int32_t value);
  void setFixed64(int field, int64_t value);
  void setSfixed32(int field, int32_t value);
  void setSfixed64(int field, int64_t value);
  void setBool(int field, bool value);
  void setString(int field, pjs::Str *value);
  void setBytes(int field, const Data &value);
  void setMessage(int field, Message *value);
  void setDoubleArray(int field, pjs::Array *values);
  void setFloatArray(int field, pjs::Array *values);
  void setInt32Array(int field, pjs::Array *values);
  void setInt64Array(int field, pjs::Array *values);
  void setUint32Array(int field, pjs::Array *values);
  void setUint64Array(int field, pjs::Array *values);
  void setSint32Array(int field, pjs::Array *values);
  void setSint64Array(int field, pjs::Array *values);
  void setFixed32Array(int field, pjs::Array *values);
  void setFixed64Array(int field, pjs::Array *values);
  void setSfixed32Array(int field, pjs::Array *values);
  void setSfixed64Array(int field, pjs::Array *values);
  void setBoolArray(int field, pjs::Array *values);
  void setStringArray(int field, pjs::Array *values);
  void setBytesArray(int field, pjs::Array *values);
  void setMessageArray(int field, pjs::Array *values);
  void serialize(Data &buffer);
  bool deserialize(const Data &data);

private:

  //
  // Message::Record
  //

  class Record : public pjs::Pooled<Record>, public List<Record>::Item {
  public:
    Record(int index, WireType type, uint64_t bits) : m_index(index), m_type(type), m_bits(bits) {}
    Record(int index, WireType type, const Data &data) : m_index(index), m_type(type), m_data(data) {}

    auto index() const -> int { return m_index; }
    auto type() const -> WireType { return m_type; }
    auto bits() const -> uint64_t { return m_bits; }
    auto data() const -> const Data& { return m_data; }

  private:
    int m_index;
    WireType m_type;
    uint64_t m_bits;
    Data m_data;
  };

  struct I32 {
    uint32_t bits;
    I32() {}
    I32(uint64_t v) : bits(v) {}
    bool read(Data::Reader &r) { return read_varint(r, bits); }
  };

  struct I64 {
    uint64_t bits;
    I64() {}
    I64(uint64_t v) : bits(v) {}
    bool read(Data::Reader &r) { return read_varint(r, bits); }
  };

  struct Float : public I32 {
    typedef float T;
    using I32::I32;
    T value() const { return *reinterpret_cast<const T*>(&bits); }
  };

  struct Double : public I64 {
    typedef double T;
    using I64::I64;
    T value() const { return *reinterpret_cast<const T*>(&bits); }
  };

  struct Int32 : public I32 {
    typedef int32_t T;
    using I32::I32;
    T value() const { return (T)bits; }
  };

  struct Int64 : public I64 {
    typedef int64_t T;
    using I64::I64;
    T value() const { return (T)bits; }
  };

  struct Uint32 : public I32 {
    typedef int32_t T;
    using I32::I32;
    T value() const { return (T)bits; }
  };

  struct Uint64 : public I64 {
    typedef int64_t T;
    using I64::I64;
    T value() const { return (T)bits; }
  };

  struct Sint32 : public I32 {
    typedef int32_t T;
    using I32::I32;
    T value() const { return decode_sint(bits); }
  };

  struct Sint64 : public I64 {
    typedef int64_t T;
    using I64::I64;
    T value() const { return decode_sint(bits); }
  };

  struct Fixed32 : public Int32 {
    using Int32::Int32;
    bool read(Data::Reader &r) { return read_uint32(r, bits); }
  };

  struct Fixed64 : public Int64 {
    using Int64::Int64;
    bool read(Data::Reader &r) { return read_uint64(r, bits); }
  };

  struct Sfixed32 : public Sint32 {
    using Sint32::Sint32;
    bool read(Data::Reader &r) { return read_uint32(r, bits); }
  };

  struct Sfixed64 : public Sint64 {
    using Sint64::Sint64;
    bool read(Data::Reader &r) { return read_uint64(r, bits); }
  };

  struct Bool : public I64 {
    typedef bool T;
    using I64::I64;
    T value() const { return (T)bits; }
  };

  template<class T> auto get_scalar(int field, WireType type) const -> typename T::T;
  template<class T> auto get_scalar_array(int field, WireType type) const -> pjs::Array*;

  std::map<int, List<Record>> m_records;

  auto get_all_records(int field, WireType type) const -> Record*;
  auto get_tail_record(int field, WireType type) const -> Record*;

  static auto read_record(Data::Reader &r) -> Record*;
  static bool read_varint(Data::Reader &r, uint64_t &n);
  static bool read_varint(Data::Reader &r, uint32_t &n);
  static bool read_uint32(Data::Reader &r, uint32_t &n);
  static bool read_uint64(Data::Reader &r, uint64_t &n);
  static auto decode_sint(uint32_t n) -> int32_t;
  static auto decode_sint(uint64_t n) -> int64_t;
  static auto encode_sint(int32_t n) -> uint32_t;
  static auto encode_sint(int64_t n) -> uint64_t;

  friend class pjs::ObjectTemplate<Message>;
};

} // namespace protobuf
} // namespace pipy

#endif // PROTOBUF_HPP
