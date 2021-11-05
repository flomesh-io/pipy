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

#include "merge.hpp"
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "listener.hpp"
#include "logging.hpp"

namespace pipy {

//
// Merge
//

Merge::Merge(const pjs::Value &key)
  : MuxBase(key)
{
}

Merge::Merge(const Merge &r)
  : MuxBase(r)
{
}

Merge::~Merge() {
}

void Merge::dump(std::ostream &out) {
  out << "merge";
}

auto Merge::clone() -> Filter* {
  return new Merge(*this);
}

//
// Merge::Session
//

void Merge::Session::open(Pipeline *pipeline) {
  pipeline->chain(m_ef_demux.input());
}

auto Merge::Session::stream(MessageStart *start) -> Stream* {
  return new Stream(this, start);
}

void Merge::Session::on_demux(Event *evt) {
  if (evt->is<StreamEnd>()) {
    close();
  }
}

//
// Merge::Session::Stream
//

void Merge::Session::Stream::on_event(Event *evt) {
  if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_start) {
      auto out = m_session->input();
      output(m_start, out);
      if (!m_buffer.empty()) output(Data::make(m_buffer), out);
      output(evt, out);
      m_start = nullptr;
      m_buffer.clear();
    }
  }
}

void Merge::Session::Stream::close() {
  delete this;
}

} // namespace pipy
