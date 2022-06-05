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

#include "link-input.hpp"
#include "pipeline.hpp"
#include "context.hpp"
#include "logging.hpp"

namespace pipy {

//
// LinkInput
//

LinkInput::LinkInput(pjs::Function *callback)
  : m_callback(callback)
{
}

LinkInput::LinkInput(const LinkInput &r)
  : Filter(r)
  , m_callback(r.m_callback)
{
}

LinkInput::~LinkInput()
{
}

void LinkInput::dump(std::ostream &out) {
  out << "input";
}

auto LinkInput::clone() -> Filter* {
  return new LinkInput(*this);
}

void LinkInput::reset() {
  Filter::reset();
  m_output = nullptr;
  m_pipeline = nullptr;
}

void LinkInput::process(Event *evt) {
  if (!m_output) {
    m_output = Output::make(output());
    m_pipeline = sub_pipeline(0, false);
    if (m_pipeline) m_pipeline->output(m_output);
    if (m_callback) {
      pjs::Value arg(m_output), ret;
      callback(m_callback, 1, &arg, ret);
    }
  }

  if (m_pipeline) {
    m_pipeline->input()->input(evt);
  }
}

} // namespace pipy
