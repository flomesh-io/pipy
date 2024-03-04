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

#ifndef API_C_STRUCT_HPP
#define API_C_STRUCT_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"
#include "options.hpp"

namespace pipy {

//
// CStructBase
//

class CStructBase : public pjs::ObjectTemplate<CStructBase> {
public:
  auto size() -> size_t { return m_size; }
  void add_fields(pjs::Object *fields);
  void add_field(pjs::Str *name, const char *type);
  void add_field(pjs::Str *name, CStructBase *type);
  auto encode(pjs::Object *values) -> Data*;
  auto decode(const Data &data) -> pjs::Object*;
  auto reflect() -> pjs::Object*;

  class FieldReflection : public pjs::ObjectTemplate<FieldReflection> {
  public:
    int offset = 0;
    int size = 0;
    int count = 0;
    bool isArray = false;
    bool isIntegral = false;
    bool isUnsigned = false;
  };

protected:
  CStructBase(bool is_union)
    : m_is_union(is_union) {}

private:
  struct Field {
    size_t offset;
    size_t size;
    size_t count;
    bool is_array;
    bool is_integral;
    bool is_unsigned;
    pjs::Value::Type type;
    pjs::Ref<CStructBase> layout;
    pjs::Ref<pjs::Str> name;
  };

  bool m_is_union;
  std::list<Field> m_fields;
  size_t m_size = 0;

  static auto align(size_t offset, size_t alignment) -> size_t;
  static auto align_size(size_t size) -> size_t;
  static void zero(Data::Builder &db, size_t count);
  static void encode(Data::Builder &db, pjs::Object *values, CStructBase *layout);
  static void encode(Data::Builder &db, int size, bool is_integral, bool is_unsigned, const pjs::Value &value);
  static auto decode(Data::Reader &dr, CStructBase *layout) -> pjs::Object*;
  static void decode(Data::Reader &dr, const Field &field, pjs::Object *values);
  static void decode(Data::Reader &dr, int size, bool is_integral, bool is_unsigned, pjs::Value &value);

  friend class pjs::ObjectTemplate<CStructBase>;
};

//
// CStruct
//

class CStruct : public pjs::ObjectTemplate<CStruct, CStructBase> {
  CStruct() : pjs::ObjectTemplate<CStruct, CStructBase>(false) {}
  friend class pjs::ObjectTemplate<CStruct, CStructBase>;
};

//
// CUnion
//

class CUnion : public pjs::ObjectTemplate<CUnion, CStructBase> {
  CUnion() : pjs::ObjectTemplate<CUnion, CStructBase>(true) {}
  friend class pjs::ObjectTemplate<CUnion, CStructBase>;
};

} // namespace pipy

#endif // API_C_STRUCT_HPP
