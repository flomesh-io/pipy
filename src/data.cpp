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

#include "data.hpp"

#include <stdexcept>

namespace pipy {

auto Data::flush() -> Data* {
  static pjs::Ref<Data> s_flush(Data::make());
  return s_flush;
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void EnumDef<pipy::Data::Encoding>::init() {
  define(pipy::Data::Encoding::UTF8, "utf8");
  define(pipy::Data::Encoding::Hex, "hdex");
  define(pipy::Data::Encoding::Base64, "base64");
  define(pipy::Data::Encoding::Base64Url, "base64url");
}

template<> void ClassDef<pipy::Data>::init() {
  super<Event>();

  ctor([](Context &ctx) -> Object* {
    int size = 0;
    Str *str, *encoding = nullptr;
    pipy::Data *data;
    try {
      if (ctx.try_arguments(0, &size)) {
        return pipy::Data::make(size);
      } else if (ctx.try_arguments(1, &str, &encoding)) {
        auto enc = EnumDef<pipy::Data::Encoding>::value(encoding, pipy::Data::Encoding::UTF8);
        if (int(enc) < 0) {
          ctx.error("unknown encoding");
          return nullptr;
        }
        return pipy::Data::make(str->str(), enc);
      } else if (ctx.try_arguments(1, &data, &encoding) && data) {
        return pipy::Data::make(*data);
      }
      ctx.error_argument_type(0, "a number or a string or a Data object");
      return nullptr;
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  accessor("size", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Data>()->size()); });

  method("push", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj);
    pipy::Data *data;
    Str *str;
    if (ctx.try_arguments(1, &data) && data) {
      obj->as<pipy::Data>()->push(*data->as<pipy::Data>());
    } else if (ctx.try_arguments(1, &str)) {
      obj->as<pipy::Data>()->push(str->str());
    } else {
      ctx.error_argument_type(0, "a Data or a string");
    }
  });

  method("shift", [](Context &ctx, Object *obj, Value &ret) {
    int count;
    if (!ctx.arguments(1, &count)) return;
    auto *out = pipy::Data::make();
    obj->as<pipy::Data>()->shift(count, *out);
    ret.set(out);
  });
}

template<> void ClassDef<Constructor<pipy::Data>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs