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
    if (!m_streams.tail() || m_streams.tail()->m_input_end) {
      auto stream = new Stream(this, start);
      m_streams.push(stream);
      output(start, stream->m_pipeline->input());
    }

  } else if (auto *data = evt->as<Data>()) {
    if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        output(data, stream->m_pipeline->input());
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        output(evt, stream->m_pipeline->input());
        stream->m_input_end = true;
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        output(MessageEnd::make(), stream->m_pipeline->input());
        stream->m_input_end = true;
      }
    }
  }
}

//
// DemuxFunction::Stream
//

DemuxFunction::Stream::Stream(DemuxFunction *demux, MessageStart *start)
  : m_demux(demux)
{
  auto p = demux->on_new_sub_pipeline();
  p->chain(EventTarget::input());
  m_pipeline = p;
}

DemuxFunction::Stream::~Stream() {
  Pipeline::auto_release(m_pipeline);
}

void DemuxFunction::Stream::on_event(Event *evt) {
  bool is_head = (
    m_demux->m_streams.head() == this
  );

  if (m_output_end) return;

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

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      if (is_head) {
        m_demux->m_streams.remove(this);
        m_demux->output(evt);
        m_demux->flush();
        delete this;
      } else {
        m_output_end = true;
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (is_head) {
      m_demux->m_streams.remove(this);
      if (!m_start) m_demux->output(MessageStart::make());
      m_demux->output(MessageEnd::make());
      m_demux->flush();
      delete this;
    }
    m_output_end = true;
  }
}

void DemuxFunction::flush() {
  auto &streams = m_streams;
  while (auto stream = streams.head()) {
    if (stream->m_start) {
      output(stream->m_start);
      if (!stream->m_buffer.empty()) output(Data::make(stream->m_buffer));
    }
    if (stream->m_output_end) {
      streams.remove(stream);
      output(MessageEnd::make());
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
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
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
  DemuxFunction::chain(Filter::output());
}

void Demux::reset() {
  Filter::reset();
  DemuxFunction::reset();
}

void Demux::process(Event *evt) {
  Filter::output(evt, DemuxFunction::input());
}

auto Demux::on_new_sub_pipeline() -> Pipeline* {
  return sub_pipeline(0, true);
}

} // namespace pipy
