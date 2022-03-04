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

List<Data::Producer> Data::Producer::s_all_producers;
Data::Producer Data::s_unknown_producer("Unknown");

auto Data::flush() -> Data* {
  static pjs::Ref<Data> s_flush(Data::make());
  return s_flush;
}

void Data::pack(const Data &data, Producer *producer, double vacancy) {
  if (&data == this) return;
  if (!producer) producer = &s_unknown_producer;
  auto occupancy = DATA_CHUNK_SIZE - int(DATA_CHUNK_SIZE * vacancy);
  for (auto view = data.m_head; view; view = view->next) {
    auto tail = m_tail;
    if (!tail) {
      push_view(new View(view));
      continue;
    }
    auto tail_offset = tail->offset;
    auto tail_length = tail->length;
    if (tail_length < occupancy || view->length + tail_length <= DATA_CHUNK_SIZE) {
      if (tail_offset > 0 || tail->chunk->retain_count > 1) {
        tail = tail->clone(producer);
        delete pop_view();
        push_view(tail);
      }
      auto tail_room = DATA_CHUNK_SIZE - tail_length;
      auto length = std::min(view->length, int(tail_room));
      std::memcpy(
        tail->chunk->data + tail_length,
        view->chunk->data + view->offset,
        length
      );
      tail->length += length;
      if (length < view->length) {
        push_view(
          new View(
            view->chunk,
            view->offset + length,
            view->length - length
          )
        );
      }
    } else {
      push_view(new View(view));
    }
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void EnumDef<pipy::Data::Encoding>::init() {
  define(pipy::Data::Encoding::UTF8, "utf8");
  define(pipy::Data::Encoding::Hex, "hex");
  define(pipy::Data::Encoding::Base64, "base64");
  define(pipy::Data::Encoding::Base64Url, "base64url");
}

static pipy::Data::Producer s_dp("Script");

template<> void ClassDef<pipy::Data>::init() {
  super<Event>();

  ctor([](Context &ctx) -> Object* {
    Array *arr;
    Str *str, *encoding = nullptr;
    pipy::Data *data;
    try {
      if (ctx.argc() == 0) {
        return pipy::Data::make();
      } else if (ctx.try_arguments(1, &arr)) {
        auto data = pipy::Data::make();
        for (int i = 0, n = arr->length(); i < n; i++) {
          Value v; arr->get(i, v);
          s_dp.push(data, uint8_t(v.to_number()));
        }
        return data;
      } else if (ctx.try_arguments(1, &str, &encoding)) {
        auto enc = EnumDef<pipy::Data::Encoding>::value(encoding, pipy::Data::Encoding::UTF8);
        if (int(enc) < 0) {
          ctx.error("unknown encoding");
          return nullptr;
        }
        return s_dp.make(str->str(), enc);
      } else if (ctx.try_arguments(1, &data, &encoding) && data) {
        return pipy::Data::make(*data);
      }
      ctx.error_argument_type(0, "a string, Array or Data");
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
      s_dp.push(obj->as<pipy::Data>(), str->str());
    } else {
      ctx.error_argument_type(0, "a Data or a string");
    }
  });

  method("pushUInt32BE", [](Context &ctx, Object *obj, Value &ret) {
    int n; if (!ctx.arguments(1, &n)) return;
    uint8_t buf[4];
    buf[0] = (uint32_t)n >> 24;
    buf[1] = (uint32_t)n >> 16;
    buf[2] = (uint32_t)n >> 8;
    buf[3] = (uint32_t)n >> 0;
    s_dp.push(obj->as<pipy::Data>(), buf, 4);
    ret.set(obj);
  });

  method("shift", [](Context &ctx, Object *obj, Value &ret) {
    int count;
    if (!ctx.arguments(1, &count)) return;
    auto *out = pipy::Data::make();
    obj->as<pipy::Data>()->shift(count, *out);
    ret.set(out);
  });

  method("shiftUInt32BE", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data buf;
    obj->as<pipy::Data>()->shift(4, buf);
    uint32_t n = 0;
    buf.scan(
      [&](int ub) {
        n = (n << 8) + ub;
        return true;
      }
    );
    ret.set(n);
  });

  method("toString", [](Context &ctx, Object *obj, Value &ret) {
    Str *encoding = nullptr;
    if (!ctx.arguments(0, &encoding)) return;
    auto enc = EnumDef<pipy::Data::Encoding>::value(encoding, pipy::Data::Encoding::UTF8);
    if (int(enc) < 0) {
      ctx.error("unknown encoding");
      return;
    }
    try {
      ret.set(obj->as<pipy::Data>()->to_string(enc));
    } catch (std::runtime_error &err) {
      ret = pjs::Value::undefined;
    }
  });
}

template<> void ClassDef<Constructor<pipy::Data>>::init() {
  super<Function>();
  ctor();

  method("from", [](Context &ctx, Object *obj, Value &ret) {
    Str *str, *encoding = nullptr;
    pipy::Data *data;
    try {
      if (!ctx.arguments(1, &str, &encoding)) return;
      auto enc = EnumDef<pipy::Data::Encoding>::value(encoding, pipy::Data::Encoding::UTF8);
      if (int(enc) < 0) {
        ctx.error("unknown encoding");
        return;
      }
      ret.set(s_dp.make(str->str(), enc));
    } catch (std::runtime_error &err) {
      ret = Value::null;
    }
  });
}

} // namespace pjs
