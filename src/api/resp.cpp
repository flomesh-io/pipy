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

#include "resp.hpp"

#include <cstdio>
#include <functional>

namespace pipy {

//
// RESP
//

thread_local static Data::Producer s_dp("RESP");

void RESP::encode(const pjs::Value &value, Data &data) {
  Data::Builder db(data, &s_dp);
  encode(value, db);
  db.flush();
}

void RESP::encode(const pjs::Value &value, Data::Builder &db) {
  std::function<void(const pjs::Value &)> write_value;
  write_value = [&](const pjs::Value &value) {
    if (value.is_string()) {
      db.push('+');
      db.push(value.s()->str());
      db.push('\r');
      db.push('\n');

    } else if (value.is_number()) {
      char buf[100];
      db.push(buf, std::snprintf(buf, sizeof(buf), ":%lld\r\n", (long long)value.n()));

    } else if (value.is_array()) {
      auto *a = value.as<pjs::Array>();
      char buf[100];
      db.push(buf, std::snprintf(buf, sizeof(buf), "*%d\r\n", (int)a->length()));
      a->iterate_all(
        [&](pjs::Value &v, int) {
          write_value(v);
        }
      );

    } else if (value.is<Data>()) {
      auto *data = value.as<Data>();
      char buf[100];
      db.push(buf, std::snprintf(buf, sizeof(buf), "$%d\r\n", (int)data->size()));
      db.push(*data, 0);
      db.push('\r');
      db.push('\n');

    } else if (value.is<pjs::Error>()) {
      db.push('-');
      db.push(value.as<pjs::Error>()->message()->str());
      db.push('\r');
      db.push('\n');

    } else if (value.is_nullish()) {
      db.push("$-1\r\n", 5);

    } else {
      auto *s = value.to_string();
      db.push('+');
      db.push(s->str());
      db.push('\r');
      db.push('\n');
      s->release();
    }
  };

  write_value(value);
  db.flush();
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// RESP
//

template<> void ClassDef<RESP>::init() {
  ctor();

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    if (!ctx.arguments(1, &val)) return;
    auto *data = pipy::Data::make();
    RESP::encode(val, *data);
    ret.set(data);
  });
}

} // namespace pjs
