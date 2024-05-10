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

auto Data::Producer::unknown() -> Producer* {
  return &s_unknown_producer;
}

void Data::pack(const Data &data, Producer *producer, double vacancy) {
  assert_same_thread(*this);
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
      m_size += length;
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

auto Data::to_string(Encoding encoding) const -> std::string {
  assert_same_thread(*this);
  switch (encoding) {
    case Encoding::utf8: {
      pjs::Utf8Decoder decoder([](int) {});
      for (const auto c : chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        for (int i = 0; i < len; i++) {
          if (!decoder.input(ptr[i])) {
            throw std::runtime_error("invalid UTF-8 encoding");
          }
        }
      }
      return to_string();
    }
    case Encoding::utf16be:
    case Encoding::utf16le: {
      std::string str;
      utils::Utf16Decoder decoder(
        encoding == Encoding::utf16be,
        [&](uint32_t code) {
          char buf[4];
          auto len = pjs::Utf8Decoder::encode(code, buf, sizeof(buf));
          str.append(buf, len);
        }
      );
      for (const auto c : chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        for (int i = 0; i < len; i++) decoder.input(ptr[i]);
      }
      return str;
    }
    case Encoding::hex: {
      std::string str;
      utils::HexEncoder encoder([&](char c) { str += c; });
      for (const auto c : chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        for (int i = 0; i < len; i++) encoder.input(ptr[i]);
      }
      return str;
    }
    case Encoding::base64: {
      std::string str;
      utils::Base64Encoder encoder([&](char c) { str += c; });
      for (const auto c : chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        for (int i = 0; i < len; i++) encoder.input(ptr[i]);
      }
      encoder.flush();
      return str;
    }
    case Encoding::base64url: {
      std::string str;
      utils::Base64UrlEncoder encoder([&](char c) { str += c; });
      for (const auto c : chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        for (int i = 0; i < len; i++) encoder.input(ptr[i]);
      }
      encoder.flush();
      return str;
    }
    default: return to_string();
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void EnumDef<pipy::Data::Encoding>::init() {
  define(pipy::Data::Encoding::utf8, "utf8");
  define(pipy::Data::Encoding::utf16be, "utf16be");
  define(pipy::Data::Encoding::utf16le, "utf16le");
  define(pipy::Data::Encoding::hex, "hex");
  define(pipy::Data::Encoding::base64, "base64");
  define(pipy::Data::Encoding::base64url, "base64url");
}

static pipy::Data::Producer s_dp("Script");

template<> void ClassDef<pipy::Data>::init() {
  super<Event>();

  ctor([](Context &ctx) -> Object* {
    Array *arr;
    Str *str;
    EnumValue<pipy::Data::Encoding> encoding;
    pipy::Data *data;
    try {
      switch (ctx.argc()) {
        case 0:
          return pipy::Data::make();
        case 1:
          if (ctx.get(0, str)) {
            return pipy::Data::make(str->str(), &s_dp);
          } else if (ctx.get(0, arr)) {
            data = pipy::Data::make();
            pipy::Data::Builder db(*data, &s_dp);
            for (int i = 0, n = arr->length(); i < n; i++) {
              Value v; arr->get(i, v);
              db.push(v.to_int32());
            }
            db.flush();
            return data;
          } else if (ctx.get(0, data)) {
            return pipy::Data::make(*data);
          } else {
            ctx.error_argument_type(0, "a string, an array or a Data");
            return nullptr;
          }
        default:
          if (!ctx.arguments(2, &str, &encoding)) return nullptr;
          return s_dp.make(str->str(), encoding);
      }
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

  method("shift", [](Context &ctx, Object *obj, Value &ret) {
    int count;
    if (!ctx.arguments(1, &count)) return;
    auto *out = pipy::Data::make();
    obj->as<pipy::Data>()->shift(count, *out);
    ret.set(out);
  });

  method("shiftTo", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Function *scanner;
    if (!ctx.arguments(1, &scanner)) return;
    auto *out = pipy::Data::make();
    obj->as<pipy::Data>()->shift_to(
      [&](int c) {
        pjs::Value arg(c), ret;
        (*scanner)(ctx, 1, &arg, ret);
        if (!ctx.ok()) return true;
        return ret.to_boolean();
      },
      *out
    );
    ret.set(out);
  });

  method("shiftWhile", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Function *scanner;
    if (!ctx.arguments(1, &scanner)) return;
    auto *out = pipy::Data::make();
    obj->as<pipy::Data>()->shift_while(
      [&](int c) {
        pjs::Value arg(c), ret;
        (*scanner)(ctx, 1, &arg, ret);
        if (!ctx.ok()) return false;
        return ret.to_boolean();
      },
      *out
    );
    ret.set(out);
  });

  method("toArray", [](Context &ctx, Object *obj, Value &ret) {
    auto data = obj->as<pipy::Data>();
    auto a = Array::make(data->size());
    auto p = 0;
    data->to_chunks(
      [&](const uint8_t *ptr, int len) {
        for (int i = 0; i < len; i++) {
          a->set(p++, ptr[i]);
        }
      }
    );
    ret.set(a);
  });

  method("toString", [](Context &ctx, Object *obj, Value &ret) {
    EnumValue<pipy::Data::Encoding> encoding = pipy::Data::Encoding::utf8;
    if (!ctx.arguments(0, &encoding)) return;
    try {
      ret.set(obj->as<pipy::Data>()->to_string(encoding));
    } catch (std::runtime_error &err) {
      ret = pjs::Value::undefined;
    }
  });
}

template<> void ClassDef<Constructor<pipy::Data>>::init() {
  super<Function>();
  ctor();

  method("from", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    EnumValue<pipy::Data::Encoding> encoding = pipy::Data::Encoding::utf8;
    try {
      if (!ctx.arguments(1, &str, &encoding)) return;
      ret.set(s_dp.make(str->str(), encoding));
    } catch (std::runtime_error &err) {
      ret = Value::null;
    }
  });
}

} // namespace pjs
