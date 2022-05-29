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
  m_shutdown = false;
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

void QueueDemuxer::shutdown() {
  if (m_streams.empty()) {
    output(StreamEnd::make());
  } else {
    m_shutdown = true;
  }
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
      if (!stream->m_buffer.empty()) {
        output(Data::make(std::move(stream->m_buffer)));
      }
    }
    if (stream->m_output_end) {
      streams.remove(stream);
      output(stream->m_end);
      delete stream;
    } else {
      break;
    }
  }
  if (m_shutdown && streams.empty()) {
    output(StreamEnd::make());
  }
}

//
// QueueDemuxer::Stream
//
// Construction:
//   - When QueueDemuxer receives a new MessageStart
//
// Destruction:
//   - When its pipeline outputs MessageEnd/StreamEnd
//   - When QueueDemuxer is reset
//

QueueDemuxer::Stream::Stream(QueueDemuxer *demuxer)
  : m_demuxer(demuxer)
{
  auto p = demuxer->on_new_sub_pipeline();
  EventSource::chain(p->input());
  p->chain(EventSource::reply());
  m_pipeline = p;
}

QueueDemuxer::Stream::~Stream() {
  Pipeline::auto_release(m_pipeline);
}

void QueueDemuxer::Stream::on_reply(Event *evt) {
  auto demuxer = m_demuxer;

  if (m_isolated) {
    demuxer->output(evt);
    return;
  }

  bool is_head = (
    demuxer->m_ordered &&
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

  } else if (auto end = evt->as<MessageEnd>()) {
    if (m_start) {
      m_end = end;
      if (is_head) {
        demuxer->m_streams.remove(this);
        demuxer->output(evt);
        demuxer->flush();
        delete this;
      } else if (demuxer->m_ordered) {
        m_output_end = true;
      } else {
        flush();
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (is_head) {
      demuxer->m_streams.remove(this);
      if (!m_start) demuxer->output(MessageStart::make());
      demuxer->output(MessageEnd::make());
      demuxer->flush();
      delete this;
    } else if (demuxer->m_ordered) {
      m_output_end = true;
    } else {
      flush();
    }
  }
}

void Demux::Stream::flush() {
  auto demuxer = m_demuxer;
  auto &streams = demuxer->m_streams;
  streams.remove(this);
  demuxer->output(m_start ? m_start.get() : MessageStart::make());
  if (!m_buffer.empty()) {
    demuxer->output(Data::make(std::move(m_buffer)));
  }
  demuxer->output(m_end ? m_end.get() : MessageEnd::make());
  delete this;
  if (demuxer->m_shutdown && streams.empty()) {
    demuxer->output(StreamEnd::make());
  }
}

//
// Demux
//

Demux::Demux()
  : QueueDemuxer(false)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , QueueDemuxer(false)
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

void Demux::shutdown() {
  Filter::shutdown();
  QueueDemuxer::shutdown();
}

auto Demux::on_new_sub_pipeline() -> Pipeline* {
  return sub_pipeline(0, true);
}

//
// DemuxQueue
//

DemuxQueue::DemuxQueue()
  : QueueDemuxer(true)
{
}

DemuxQueue::DemuxQueue(const DemuxQueue &r)
  : Filter(r)
  , QueueDemuxer(true)
{
}

DemuxQueue::~DemuxQueue()
{
}

void DemuxQueue::dump(std::ostream &out) {
  out << "demuxQueue";
}

auto DemuxQueue::clone() -> Filter* {
  return new DemuxQueue(*this);
}

void DemuxQueue::chain() {
  Filter::chain();
  QueueDemuxer::chain(Filter::output());
}

void DemuxQueue::reset() {
  Filter::reset();
  QueueDemuxer::reset();
}

void DemuxQueue::process(Event *evt) {
  Filter::output(evt, QueueDemuxer::input());
}

void DemuxQueue::shutdown() {
  Filter::shutdown();
  QueueDemuxer::shutdown();
}

auto DemuxQueue::on_new_sub_pipeline() -> Pipeline* {
  return sub_pipeline(0, true);
}

} // namespace pipy
