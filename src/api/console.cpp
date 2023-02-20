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

#include "console.hpp"
#include "data.hpp"
#include "log.hpp"

#include <sstream>

namespace pipy {

thread_local static Data::Producer s_dp("Console");

void Console::log(const pjs::Value *values, int count) {
  Data buf;
  Data::Builder db(buf, &s_dp);
  char str[100];
  db.push(str, Log::format_header(Log::INFO, str, sizeof(str)));
  for (int i = 0; i < count; i++) {
    if (i > 0) db.push(' ');
    auto &v = values[i];
    if (v.is_string()) {
      db.push(v.s()->str());
    } else {
      dump(v, db);
    }
  }
  db.flush();
  Log::write(buf);
}

void Console::dump(const pjs::Value &value, Data &out) {
  Data::Builder db(out, &s_dp);
  dump(value, db);
  db.flush();
}

void Console::dump(const pjs::Value &value, Data::Builder &db) {
  static const std::string s_empty("empty");
  static const std::string s_undefined("undefined");
  static const std::string s_true("true");
  static const std::string s_false("false");
  static const std::string s_null("null");
  static const std::string s_data("Data");
  static const std::string s_comma(", ");
  static const std::string s_colon(": ");
  static const std::string s_hex_numbers("0123456789abcdef");

  switch (value.type()) {
    case pjs::Value::Type::Empty: db.push(s_empty); break;
    case pjs::Value::Type::Undefined: db.push(s_undefined); break;
    case pjs::Value::Type::Boolean: db.push(value.b() ? s_true : s_false); break;
    case pjs::Value::Type::Number: {
      char str[100];
      auto len = pjs::Number::to_string(str, sizeof(str), value.n());
      db.push(str, len);
      break;
    }
    case pjs::Value::Type::String: {
      db.push('"');
      utils::escape(value.s()->str(), [&](char c) { db.push(c); });
      db.push('"');
      break;
    }
    case pjs::Value::Type::Object: {
      auto obj = value.o();
      if (!obj) {
        db.push(s_null);
      } else if (obj->is<pjs::Array>()) {
        auto a = obj->as<pjs::Array>();
        auto p = 0;
        bool first = true;
        auto push_empty = [&](int n) {
          db.push(s_empty);
          if (n > 1) {
            char str[100];
            auto len = std::snprintf(str, sizeof(str), " x %d times", n);
            db.push(str, len);
          }
        };
        db.push('[');
        db.push(' ');
        a->iterate_all(
          [&](pjs::Value &v, int i) {
            if (first) first = false; else db.push(s_comma);
            if (i > p) push_empty(i - p);
            dump(v, db);
            p = i + 1;
          }
        );
        int n = a->length() - p;
        if (n > 0) {
          if (!first) db.push(s_comma);
          push_empty(n);
        }
        db.push(' ');
        db.push(']');
      } else if (obj->is<Data>()) {
        Data::Reader r(*obj->as<Data>());
        db.push(s_data);
        db.push('[');
        for (int i = 0; i < 10; i++) {
          int c = r.get();
          if (c < 0) break;
          db.push(' ');
          db.push(s_hex_numbers[0xf & (c>>4)]);
          db.push(s_hex_numbers[0xf & (c>>0)]);
        }
        if (!r.eof()) {
          int n = obj->as<Data>()->size() - 10;
          if (n == 1) {
            int c = r.get();
            db.push(' ');
            db.push(s_hex_numbers[0xf & (c>>4)]);
            db.push(s_hex_numbers[0xf & (c>>0)]);
          } else {
            char str[100];
            auto len = std::snprintf(str, sizeof(str), " ... and %d more bytes", n);
            db.push(str, len);
          }
        }
        db.push(' ');
        db.push(']');

      } else {
        auto t = obj->type();
        if (t != pjs::class_of<pjs::Object>()) {
          db.push(t->name()->str());
        }
        bool first = true;
        db.push('{');
        db.push(' ');
        for (int i = 0, n = t->field_count(); i < n; i++) {
          auto f = t->field(i);
          if (f->type() == pjs::Field::Variable ||
              f->type() == pjs::Field::Accessor
          ) {
            if (first) first = false; else db.push(s_comma);
            db.push(f->name()->str());
            db.push(s_colon);
            if (f->type() == pjs::Field::Accessor) {
              pjs::Value v;
              static_cast<pjs::Accessor*>(f)->get(obj, v);
              dump(v, db);
            } else {
              dump(obj->data()->at(i), db);
            }
          }
        }
        obj->iterate_hash(
          [&](pjs::Str *k, pjs::Value &v) {
            if (first) first = false; else db.push(s_comma);
            db.push('"');
            utils::escape(k->str(), [&](char c) { db.push(c); });
            db.push('"');
            db.push(s_colon);
            dump(v, db);
            return true;
          }
        );
        db.push(' ');
        db.push('}');
      }
      break;
    }
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Console>::init() {
  ctor();

  // console.log
  method("log", [](Context &ctx, Object *, Value &result) {
    Console::log(&ctx.arg(0), ctx.argc());
  });

}

} // namespace pjs
