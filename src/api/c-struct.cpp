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

//
// CStruct::Options
//

CStruct::Options::Options(pjs::Object *options) {
  Value(options, "isUnion")
    .get(is_union)
    .check_nullable();
  Value(options, "bigEndian")
    .get(big_endian)
    .check_nullable();
}

//
// CStruct
//

void CStruct::field(const char *type, pjs::Str *name) {
}

void CStruct::field(CStruct *type, pjs::Str *name) {
}

auto CStruct::encode(pjs::Object *obj) -> Data* {
  return nullptr;
}

auto CStruct::decode(const Data &data) -> pjs::Object* {
  return nullptr;
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
