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
#include "log.hpp"

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
      stream->m_responses.push(new Response);
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
    shutdown();
  }
}

void QueueDemuxer::flush() {
  auto &streams = m_streams;
  while (auto stream = streams.head()) {
    auto &responses = stream->m_responses;
    while (auto *r = responses.head()) {
      if (r->start) {
        output(r->start);
        if (!r->buffer.empty()) {
          output(Data::make(std::move(r->buffer)));
        }
      }
      if (r->end) {
        responses.remove(r);
        output(r->end);
        delete r;
      } else {
        break;
      }
    }
    if (responses.empty()) {
      streams.remove(stream);
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
  while (auto *r = m_responses.head()) {
    m_responses.remove(r);
    delete r;
  }
  Pipeline::auto_release(m_pipeline);
}

void QueueDemuxer::Stream::on_reply(Event *evt) {
  auto demuxer = m_demuxer;

  if (m_isolated) {
    demuxer->output(evt);
    return;
  }

  auto r = m_responses.head();
  while (r && r->end) r = r->next();
  if (!r) return;

  bool is_head = (demuxer->m_streams.head() == this);

  if (auto start = evt->as<MessageStart>()) {
    if (!r->start) {
      r->start = start;
      if (!demuxer->on_reply_start(start)) {
        m_responses.push(new Response);
      }
      if (is_head) {
        demuxer->output(evt);
      }
    }

  } else if (auto data = evt->as<Data>()) {
    if (r->start) {
      if (is_head) {
        demuxer->output(evt);
      } else {
        r->buffer.push(*data);
      }
    }

  } else if (auto end = evt->as<MessageEnd>()) {
    if (r->start) {
      r->end = end;
      if (is_head) {
        m_responses.remove(r);
        delete r;
        bool is_stream_end = m_responses.empty();
        if (is_stream_end) demuxer->m_streams.remove(this);
        demuxer->output(evt);
        demuxer->flush();
        if (is_stream_end) delete this;
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (is_head) {
      demuxer->m_streams.remove(this);
      if (!r->start) demuxer->output(MessageStart::make());
      demuxer->output(MessageEnd::make());
      demuxer->flush();
      delete this;
    } else {
      if (!r->start) r->start = MessageStart::make();
      r->end = MessageEnd::make();
      while (auto *n = r->next()) {
        m_responses.remove(n);
        delete n;
      }
    }
  }
}

//
// DemuxQueue
//

DemuxQueue::DemuxQueue()
{
}

DemuxQueue::DemuxQueue(const DemuxQueue &r)
  : Filter(r)
{
}

DemuxQueue::~DemuxQueue()
{
}

void DemuxQueue::dump(Dump &d) {
  Filter::dump(d);
  d.name = "demuxQueue";
  d.sub_type = Dump::DEMUX;
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

void Demux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "demux";
  d.sub_type = Dump::DEMUX;
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::reset() {
  Filter::reset();
  m_pipeline = nullptr;
}

void Demux::process(Event *evt) {
  if (evt->is<MessageStart>()) {
    if (!m_pipeline) {
      m_pipeline = sub_pipeline(0, true);
      m_pipeline->chain(EventSource::reply());
      m_pipeline->input()->input(evt);
    }

  } else if (evt->is<Data>()) {
    if (m_pipeline) {
      m_pipeline->input()->input(evt);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_pipeline) {
      m_pipeline->input()->input(evt);
      Pipeline::auto_release(m_pipeline);
      m_pipeline = nullptr;
    }
  }
}

} // namespace pipy
