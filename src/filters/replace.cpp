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

#include "replace.hpp"
#include "message.hpp"

namespace pipy {

//
// Replace
//

Replace::Replace(pjs::Object *replacement)
  : Handle(replacement && replacement->is_function() ? replacement->as<pjs::Function>() : nullptr)
  , m_replacement(replacement)
{
}

Replace::Replace(const Replace &r)
  : Handle(r)
  , m_replacement(r.m_replacement)
{
}

Replace::~Replace()
{
}

bool Replace::callback(pjs::Object *arg) {
  if (!m_replacement) return true;
  if (!m_replacement->is_function()) {
    if (Filter::output(m_replacement.get())) return true;
    Filter::error("replacement is not an event or Message or an array of those");
    return false;
  }
  return Handle::callback(arg);
}

bool Replace::on_callback_return(const pjs::Value &result) {
  if (!result.is_undefined()) {
    if (!result.is_object() || !Filter::output(result.o())) {
      Filter::error("callback did not return an event or Message or an array of those");
      return false;
    }
  }
  return Handle::on_callback_return(result);
}

} // namespace pipy
