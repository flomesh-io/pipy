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

#include "split.hpp"
#include "data.hpp"

namespace pipy {

//
// Split
//

Split::Split(pjs::Function *callback)
  : m_callback(callback)
{
}

Split::Split(const Split &r)
  : Filter(r)
  , m_callback(r.m_callback)
{
}

Split::~Split()
{
}

void Split::dump(std::ostream &out) {
  out << "split";
}

auto Split::clone() -> Filter* {
  return new Split(*this);
}

void Split::process(Event *evt) {

  if (auto data = evt->as<Data>()) {
    while (!data->empty()) {
      pjs::Ref<Data> buf(Data::make());
      pjs::Ref<pjs::Object> splitter;
      bool error = false;
      data->shift_to(
        [&](int c) {
          pjs::Value arg(c), ret;
          if (!callback(m_callback, 1, &arg, ret)) {
            error = true;
            return true;
          }
          if (ret.is_object()) {
            splitter = ret.o();
            return true;
          }
          return false;
        },
        *buf
      );
      if (error) return;
      output(buf);
      if (splitter) output(splitter.get());
    }

  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

} // namespace pipy
