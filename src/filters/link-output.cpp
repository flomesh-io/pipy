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

#include "link-output.hpp"
#include "pipeline.hpp"
#include "context.hpp"
#include "logging.hpp"

namespace pipy {

//
// LinkOutput
//

LinkOutput::LinkOutput(pjs::Function *output_f)
  : m_output_f(output_f)
{
}

LinkOutput::LinkOutput(const LinkOutput &r)
  : Filter(r)
  , m_output_f(r.m_output_f)
{
}

LinkOutput::~LinkOutput()
{
}

void LinkOutput::dump(Dump &d) {
  Filter::dump(d);
  d.name = "output";
}

auto LinkOutput::clone() -> Filter* {
  return new LinkOutput(*this);
}

void LinkOutput::reset() {
  Filter::reset();
  m_buffer.clear();
  m_output = nullptr;
}

void LinkOutput::process(Event *evt) {
  if (!m_output) {
    pjs::Value ret(m_output_f);
    if (m_output_f) {
      if (!eval(m_output_f, ret)) return;
    }
    if (!ret.is_undefined()) {
      if (ret.is_null()) {
        if (auto out = pipeline()->output()) {
          m_output = out;
        } else if (auto inb = context()->inbound()) {
          m_output = inb->output();
        }
      } else if (ret.is<Output>()) {
        m_output = ret.as<Output>();
      } else {
        Log::error("[output] callback did not return an Output object");
        return;
      }
    }
    if (m_output) {
      m_buffer.flush(
        [this](Event *evt) {
          m_output->input()->input(evt);
        }
      );
    }
  }

  if (m_output) {
    m_output->input()->input(evt->clone());
  } else {
    m_buffer.push(evt->clone());
  }

  output(evt);
}

} // namespace pipy
