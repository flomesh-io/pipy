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

#include "bgp.hpp"

#include <cstdio>
#include <functional>

namespace pipy {

//
// BGP
//

thread_local static Data::Producer s_dp("BGP");

auto BGP::decode(const Data &data) -> pjs::Array* {
  pjs::Array *a = pjs::Array::make();
  StreamParser sp(
    [=](const pjs::Value &value) {
      a->push(value);
    }
  );
  Data buf(data);
  sp.parse(buf);
  return a;
}

void BGP::encode(const pjs::Value &value, Data &data) {
  Data::Builder db(data, &s_dp);
  encode(value, db);
  db.flush();
}

void BGP::encode(const pjs::Value &value, Data::Builder &db) {
}

//
// BGP::Parser
//

BGP::Parser::Parser()
  : m_payload(Data::make())
{
}

void BGP::Parser::reset() {
  Deframer::reset();
  Deframer::pass_all(true);
  m_payload->clear();
}

void BGP::Parser::parse(Data &data) {
  Deframer::deframe(data);
}

auto BGP::Parser::on_state(int state, int c) -> int {
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// BGP
//

template<> void ClassDef<BGP>::init() {
  ctor();

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    ret.set(BGP::decode(*data));
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    if (!ctx.arguments(1, &val)) return;
    auto *data = pipy::Data::make();
    BGP::encode(val, *data);
    ret.set(data);
  });
}

} // namespace pjs
