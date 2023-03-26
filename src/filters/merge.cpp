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
#include "pipeline.hpp"
#include "log.hpp"

namespace pipy {

//
// Merge
//

Merge::Merge(pjs::Function *session_selector, pjs::Object *options)
  : m_session_selector(session_selector)
  , m_options(options)
{
}

Merge::Merge(const Merge &r)
  : Filter(r)
  , MuxBase(r)
  , m_session_selector(r.m_session_selector)
  , m_options(r.m_options)
{
}

Merge::~Merge()
{
}

void Merge::dump(Dump &d) {
  Filter::dump(d);
  d.name = "merge";
  d.sub_type = Dump::MUX;
  d.out_type = Dump::OUTPUT_FROM_SELF;
}

auto Merge::clone() -> Filter* {
  return new Merge(*this);
}

void Merge::reset() {
  Filter::reset();
  MuxBase::reset();
}

void Merge::process(Event *evt) {
  MuxBase::open_stream(Filter::output());
  MuxBase::write_stream(evt);
  output(evt);
}

bool Merge::on_select_session(pjs::Value &key) {
  if (m_session_selector && !eval(m_session_selector, key)) return false;
  if (key.is_undefined()) key.set(Filter::context()->inbound());
  return true;
}

auto Merge::on_new_cluster() -> MuxBase::SessionCluster* {
  return new SessionCluster(this, m_options);
}

auto Merge::on_new_pipeline(EventTarget::Input *output, pjs::Value args[2]) -> Pipeline* {
  return Filter::sub_pipeline(0, true, output, nullptr, 2, args);
}

//
// Merge::Stream
//

void Merge::Stream::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_start) {
      auto inp = m_output.get();
      inp->input(m_start);
      if (!m_buffer.empty()) {
        inp->input(Data::make(m_buffer));
        m_buffer.clear();
      }
      inp->input(MessageEnd::make());
    }
  }
}

} // namespace pipy
