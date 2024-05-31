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

#include "replace-start.hpp"

namespace pipy {

ReplaceStart::ReplaceStart(pjs::Object *replacement)
  : Replace(replacement)
{
}

ReplaceStart::ReplaceStart(const ReplaceStart &r)
  : Replace(r)
{
}

ReplaceStart::~ReplaceStart()
{
}

void ReplaceStart::dump(Dump &d) {
  Filter::dump(d);
  d.name = "replaceStreamStart";
}

auto ReplaceStart::clone() -> Filter* {
  return new ReplaceStart(*this);
}

void ReplaceStart::reset() {
  Replace::reset();
  m_started = false;
}

void ReplaceStart::handle(Event *evt) {
  if (!m_started) {
    if (!Replace::callback(evt)) return;
    m_started = true;
  } else {
    Replace::pass(evt);
  }
}

} // namespace pipy
