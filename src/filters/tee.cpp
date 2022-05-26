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

#include "tee.hpp"
#include "logging.hpp"

namespace pipy {

Tee::Tee(const pjs::Value &filename)
  : m_filename(filename)
{
}

Tee::Tee(const Tee &r)
  : Filter(r)
  , m_filename(r.m_filename)
{
}

Tee::~Tee() {
}

void Tee::dump(std::ostream &out) {
  out << "tee";
}

auto Tee::clone() -> Filter* {
  return new Tee(*this);
}

void Tee::reset() {
  Filter::reset();
  if (m_file) {
    m_file->close();
    m_file = nullptr;
  }
  m_resolved_filename = nullptr;
}

void Tee::process(Event *evt) {
  if (auto *data = evt->as<Data>()) {
    if (!m_resolved_filename) {
      pjs::Value filename;
      if (!eval(m_filename, filename)) return;
      auto *s = filename.to_string();
      m_resolved_filename = s;
      s->release();
      m_file = File::make(m_resolved_filename->str());
      m_file->open_write();
    }

    if (m_file) {
      m_file->write(*data);
    }

  } else if (evt->is<StreamEnd>()) {
    if (m_file) {
      m_file->close();
      m_file = nullptr;
    }
  }

  output(evt);
}

} // namespace pipy
