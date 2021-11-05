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

#include "demux.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "logging.hpp"

namespace pipy {

//
// DemuxFunction
//

void DemuxFunction::reset() {
  while (auto stream = m_streams.head()) {
    m_streams.remove(stream);
    delete stream;
  }
}

void DemuxFunction::on_event(Event *evt) {
  if (auto *start = evt->as<MessageStart>()) {
    if (!m_streams.tail() || m_streams.tail()->input_end()) {
      auto stream = new Stream(this, start);
      m_streams.push(stream);
    }

  } else if (auto *data = evt->as<Data>()) {
    if (auto stream = m_streams.tail()) {
      stream->data(data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (auto stream = m_streams.tail()) {
      stream->end(MessageEnd::make());
    }
  }
}

//
// DemuxFunction::Stream
//

DemuxFunction::Stream::Stream(DemuxFunction *demux, MessageStart *start)
  : m_demux(demux)
{
  m_pipeline = demux->sub_pipeline();
  m_pipeline->chain(EventTarget::input());
  demux->output(start, m_pipeline->input());
}

void DemuxFunction::Stream::data(Data *data) {
  if (!m_input_end) {
    m_demux->output(data, m_pipeline->input());
  }
}

void DemuxFunction::Stream::end(MessageEnd *end) {
  if (!m_input_end) {
    m_demux->output(end, m_pipeline->input());
    m_input_end = true;
  }
}

void DemuxFunction::Stream::on_event(Event *evt) {
  bool is_head = (
    m_demux->m_streams.head() == this
  );

  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
      if (is_head) {
        m_demux->output(evt);
      }
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      if (is_head) {
        m_demux->output(evt);
      } else {
        m_buffer.push(*data);
      }
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (is_head) {
      m_demux->m_streams.remove(this);
      m_demux->output(MessageEnd::make());
      flush();
      delete this;
    } else {
      if (!m_start) m_start = MessageStart::make();
      m_output_end = true;
    }
  }
}

void DemuxFunction::Stream::flush() {
  auto &streams = m_demux->m_streams;
  while (auto stream = streams.head()) {
    if (stream->m_start) {
      m_demux->output(stream->m_start);
      stream->m_start = nullptr;
      if (!stream->m_buffer.empty()) {
        m_demux->output(Data::make(stream->m_buffer));
        stream->m_buffer.clear();
      }
    }
    if (stream->m_output_end) {
      streams.remove(stream);
      m_demux->output(MessageEnd::make());
      delete stream;
    } else {
      break;
    }
  }
}

//
// Demux
//

Demux::Demux()
  : m_ef_demux(this)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , m_ef_demux(this)
{
}

Demux::~Demux()
{
}

void Demux::dump(std::ostream &out) {
  out << "demux";
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::chain() {
  Filter::chain();
  m_ef_demux.chain(output());
}

void Demux::reset() {
  Filter::reset();
  m_ef_demux.reset();
}

void Demux::process(Event *evt) {
  output(evt, m_ef_demux.input());
}

//
// Demux::DemuxInternal
//

auto Demux::DemuxInternal::sub_pipeline() -> Pipeline* {
  return demux->sub_pipeline(0, true);
}

} // namespace pipy
