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
// CStruct
//

class CStruct : public pjs::ObjectTemplate<CStruct> {
public:
  struct Options : public pipy::Options {
    bool is_union = false;
    Options() {}
    Options(pjs::Object *options);
  };

  void field(const char *type, pjs::Str *name);
  void field(CStruct *type, pjs::Str *name = nullptr);
  auto encode(pjs::Object *values) -> Data*;
  auto decode(const Data &data) -> pjs::Object*;

private:
  CStruct(const Options &options)
    : m_options(options) {}

  struct Field {
    size_t offset;
    size_t size;
    size_t count;
    bool is_array;
    bool is_integral;
    bool is_unsigned;
    pjs::Value::Type type;
    pjs::Ref<CStruct> layout;
    pjs::Ref<pjs::Str> name;
  };

  Options m_options;
  std::list<Field> m_fields;
  size_t m_size = 0;

  static auto align(size_t offset, size_t alignment) -> size_t;
  static auto align_size(size_t size) -> size_t;
  static void zero(Data::Builder &db, size_t count);
  static void encode(Data::Builder &db, pjs::Object *values, CStruct *layout);
  static void encode(Data::Builder &db, int size, bool is_integral, bool is_unsigned, pjs::Value &value);

  friend class pjs::ObjectTemplate<CStruct>;
};

} // namespace pipy

#endif // API_C_STRUCT_HPP
