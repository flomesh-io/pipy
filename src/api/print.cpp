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

#include "print.hpp"
#include "api/console.hpp"

#include <iostream>

namespace pipy {

static void print(Data::Builder &db, int argc, const pjs::Value argv[]) {
  for (int i = 0; i < argc; i++) {
    if (i > 0) db.push(' ');
    auto &v = argv[i];
    if (v.is_string()) {
      db.push(v.s()->str());
    } else {
      Console::dump(v, db);
    }
  }
}

void print(int argc, const pjs::Value argv[]) {
  Data buf;
  Data::Builder db(buf);
  print(db, argc, argv);
  db.flush();
  for (auto c : buf.chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    std::cout.write(ptr, len);
  }
  std::cout.flush();
}

void println(int argc, const pjs::Value argv[]) {
  Data buf;
  Data::Builder db(buf);
  print(db, argc, argv);
  db.push('\n');
  db.flush();
  for (auto c : buf.chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    std::cout.write(ptr, len);
  }
  std::cout.flush();
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<PrintFunction>::init() {
  super<Function>();
  ctor();
}

template<> void ClassDef<PrintlnFunction>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
