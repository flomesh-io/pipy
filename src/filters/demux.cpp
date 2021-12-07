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
// QueueDemuxer
//

void QueueDemuxer::reset() {
  while (auto stream = m_streams.head()) {
    m_streams.remove(stream);
    delete stream;
  }
  m_isolated = false;
}

void QueueDemuxer::isolate() {
  auto stream = m_streams.tail();
  if (!stream) {
    stream = new Stream(this);
    m_streams.push(stream);
  }
  stream->m_isolated = true;
  m_isolated = true;
}

void QueueDemuxer::on_event(Event *evt) {
  if (m_isolated) {
    auto stream = m_streams.tail();
    stream->output(evt);
    return;
  }

  if (auto *start = evt->as<MessageStart>()) {
    if (!m_streams.tail() || m_streams.tail()->m_input_end) {
      auto stream = new Stream(this);
      m_streams.push(stream);
      stream->output(start);
    }

  } else if (auto *data = evt->as<Data>()) {
    if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        stream->output(data);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        stream->m_input_end = true;
        stream->output(evt);
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        stream->m_input_end = true;
        stream->output(MessageEnd::make());
      }
    }
  }
}

void QueueDemuxer::flush() {
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
// QueueDemuxer::Stream
//

QueueDemuxer::Stream::Stream(QueueDemuxer *demuxer)
  : m_demuxer(demuxer)
{
  auto p = demuxer->on_new_sub_pipeline();
  EventSource::chain(p->input());
  p->chain(EventSource::reply());
  m_pipeline = p;
}

QueueDemuxer::Stream::~Stream()
{
}

void QueueDemuxer::Stream::on_event(Event *evt) {
  auto demuxer = m_demuxer;

  if (m_isolated) {
    demuxer->output(evt);
    return;
  }

  bool is_head = (
    demuxer->m_streams.head() == this
  );

  if (m_output_end) return;

  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
      if (is_head) {
        demuxer->output(evt);
      }
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      if (is_head) {
        demuxer->output(evt);
      } else {
        m_buffer.push(*data);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      if (is_head) {
        demuxer->m_streams.remove(this);
        demuxer->output(evt);
        demuxer->flush();
        delete this;
      } else {
        m_output_end = true;
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (is_head) {
      demuxer->m_streams.remove(this);
      if (!m_start) demuxer->output(MessageStart::make());
      demuxer->output(MessageEnd::make());
      demuxer->flush();
      delete this;
    } else {
      m_output_end = true;
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
  QueueDemuxer::chain(Filter::output());
}

void Demux::reset() {
  Filter::reset();
  QueueDemuxer::reset();
}

void Demux::process(Event *evt) {
  Filter::output(evt, QueueDemuxer::input());
}

auto Demux::on_new_sub_pipeline() -> Pipeline* {
  return sub_pipeline(0, true);
}

} // namespace pipy
