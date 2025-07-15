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

#include "c-string.hpp"

namespace pipy {

static Data::Producer s_dp("CString");

CString::CString()
  : m_data(Data::make())
{
}

CString::CString(const std::string &str)
  : m_data(Data::make(str, &s_dp))
{
}

CString::CString(const Data &data)
  : m_data(Data::make(data))
{
}

auto CString::to_str() -> pjs::Str* {
  if (!m_str) {
    m_str = pjs::Str::make(m_data->to_string());
  }
  return m_str.get();
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<CString>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *str;
    pipy::Data *data;
    if (!ctx.argc()) return CString::make();
    if (ctx.get(0, str)) return CString::make(str->str());
    if (ctx.get(0, data)) return CString::make(*data);
    ctx.error_argument_type(0, "a string or a Data object");
    return nullptr;
  });

  accessor("size", [](Object *obj, Value &ret) { ret.set(obj->as<CString>()->size()); });
  accessor("data", [](Object *obj, Value &ret) { ret.set(obj->as<CString>()->data()); });
}

template<> void ClassDef<Constructor<CString>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
