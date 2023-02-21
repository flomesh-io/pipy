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

#ifndef JSON_HPP
#define JSON_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"

#include <functional>

namespace pipy {

//
// JSON
//

class JSON : public pjs::ObjectTemplate<JSON> {
public:

  //
  // JSON::Visitor
  //

  class Visitor {
  public:
    virtual void null() {}
    virtual void boolean(bool b) {}
    virtual void integer(int64_t i) {}
    virtual void number(double n) {}
    virtual void string(const char *s, size_t len) {}
    virtual void map_start() {}
    virtual void map_key(const char *s, size_t len) {}
    virtual void map_end() {}
    virtual void array_start() {}
    virtual void array_end() {}
  };

  static bool visit(const std::string &str, Visitor *visitor);
  static bool visit(const Data &data, Visitor *visitor);

  static bool parse(
    const std::string &str,
    const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
    pjs::Value &val
  );

  static auto stringify(
    const pjs::Value &val,
    const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
    int space
  ) -> std::string;

  static bool decode(
    const Data &data,
    const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
    pjs::Value &val
  );

  static bool encode(
    const pjs::Value &val,
    const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
    int space,
    Data &data
  );

  static bool encode(
    const pjs::Value &val,
    const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
    int space,
    Data::Builder &db
  );
};

} // namespace pipy

#endif // JSON_HPP
