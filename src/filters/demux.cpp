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

namespace pipy {

//
// Demuxer
//

void Demuxer::reset() {
  while (auto s = m_streams.head()) {
    delete s;
  }
}

auto Demuxer::open_stream(Pipeline *pipeline) -> EventFunction* {
  auto s = new Stream(this);
  s->open(pipeline);
  return s;
}

void Demuxer::close_stream(EventFunction *stream) {
  auto s = static_cast<Stream*>(stream);
  s->close();
}

//
// Demuxer::Stream
//

void Demuxer::Stream::open(Pipeline *pipeline) {
  m_pipeline = pipeline;
  EventProxy::chain_forward(pipeline->input());
  pipeline->chain(EventProxy::reply());
}

void Demuxer::Stream::close() {
  m_closed = true;
  recycle();
}

void Demuxer::Stream::on_event(Event *evt) {
  EventProxy::forward(evt);
}

void Demuxer::Stream::on_reply(Event *evt) {
  EventProxy::output(evt);
  if (evt->is<StreamEnd>()) {
    m_stream_end = true;
    recycle();
  }
}

//
// Demuxer::Queue
//

void Demuxer::Queue::reset() {
  if (m_stream) {
    on_close_stream(m_stream);
    m_stream = nullptr;
  }
  clear();
  m_closed = false;
}

void Demuxer::Queue::dedicate() {
}

void Demuxer::Queue::shutdown() {
}

void Demuxer::Queue::on_event(Event *evt) {
  if (m_closed) return;

  switch (evt->type()) {
    case Event::Type::MessageStart:
      if (!m_stream) {
        int n = on_queue_message(evt->as<MessageStart>());
        if (n >= 0) {
          if (auto s = on_open_stream()) {
            m_stream = s;
            if (n > 0) {
              auto r = new Receiver(this, n);
              s->chain(r->input());
              m_receivers.push(r);
            }
            s->input()->input(evt);
          }
        }
      }
      break;
    case Event::Type::Data:
      if (auto s = m_stream) {
        s->input()->input(evt);
      }
      break;
    case Event::Type::MessageEnd:
      if (auto s = m_stream) {
        auto input = s->input();
        input->input(evt);
        input->input(StreamEnd::make());
        on_close_stream(s);
        m_stream = nullptr;
      }
      break;
    case Event::Type::StreamEnd:
      if (auto s = m_stream) {
        s->input()->input(evt);
        on_close_stream(s);
        m_stream = nullptr;
      }
      if (m_receivers.empty()) {
        EventFunction::output(evt);
      } else {
        m_stream_end = evt->as<StreamEnd>();
      }
      break;
  }
}

void Demuxer::Queue::shift() {
  auto r = m_receivers.head();
  m_receivers.remove(r);
  delete r;
  while (auto r = m_receivers.head()) {
    if (!r->flush()) break;
    m_receivers.remove(r);
    delete r;
  }
  if (m_receivers.empty() && m_stream_end) {
    EventFunction::output(m_stream_end);
  }
}

void Demuxer::Queue::clear() {
  while (auto r = m_receivers.head()) {
    m_receivers.remove(r);
    delete r;
  }
  m_stream_end = nullptr;
}

void Demuxer::Queue::close() {
  clear();
  m_closed = true;
}

//
// Demuxer::Queue::Receiver
//

bool Demuxer::Queue::Receiver::flush() {
  m_buffer.flush(
    [this](Message *msg) {
      msg->write(m_queue->EventFunction::output());
    }
  );
  return !m_output_count;
}

void Demuxer::Queue::Receiver::on_event(Event *evt) {
  if (m_queue->m_receivers.head() == this) {
    switch (evt->type()) {
      case Event::Type::MessageStart: {
        if (!m_message_started) {
          m_queue->EventFunction::output(evt);
          m_message_started = true;
        }
        break;
      }
      case Event::Type::Data:
        if (m_message_started) {
          m_queue->EventFunction::output(evt);
        }
        break;
      case Event::Type::MessageEnd:
        if (m_message_started) {
          m_queue->EventFunction::output(evt);
          m_message_started = false;
          if (!--m_output_count) m_queue->shift();
        }
        break;
      case Event::Type::StreamEnd:
        if (m_message_started && m_output_count == 1) {
          m_queue->EventFunction::output(MessageEnd::make(evt->as<StreamEnd>()));
          m_message_started = false;
          if (!--m_output_count) m_queue->shift();
        } else {
          m_queue->EventFunction::output(evt);
          m_queue->close();
        }
        break;
    }
  } else {
    if (auto msg = m_reader.read(evt)) {
      if (m_output_count > 0) {
        m_buffer.push(msg);
        m_output_count--;
      }
      msg->release();
    }
  }
}

//
// QueueDemuxer
//

void QueueDemuxer::reset() {
  while (auto stream = m_streams.head()) {
    m_streams.remove(stream);
    delete stream;
  }
  m_one_way_pipeline = nullptr;
  m_dedicated = false;
  m_shutdown = false;
}

void QueueDemuxer::dedicate() {
  if (auto stream = m_streams.tail()) {
    stream->m_dedicated = true;
  }
  m_dedicated = true;
}

void QueueDemuxer::shutdown() {
  if (m_streams.empty()) {
    output(StreamEnd::make());
  } else {
    m_shutdown = true;
  }
}

void QueueDemuxer::on_event(Event *evt) {
  if (m_dedicated) {
    if (auto stream = m_streams.tail()) {
      stream->output(evt);
    }
    return;
  }

  if (auto *start = evt->as<MessageStart>()) {
    if (on_request_start(start)) {
      if (!m_streams.tail() || m_streams.tail()->m_input_end) {
        auto stream = new Stream(this);
        stream->m_responses.push(new Response);
        m_streams.push(stream);
        stream->output(start);
      }
    } else if (!m_one_way_pipeline) {
      auto *p = on_new_sub_pipeline(nullptr);
      m_one_way_pipeline = p;
      p->input()->input(evt);
    }

  } else if (auto *data = evt->as<Data>()) {
    if (m_one_way_pipeline) {
      m_one_way_pipeline->input()->input(evt);
    } else if (auto stream = m_streams.tail()) {
      if (!stream->m_input_end) {
        stream->output(data);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_one_way_pipeline) {
      Pipeline::auto_release(m_one_way_pipeline);
      m_one_way_pipeline->input()->input(evt);
      m_one_way_pipeline = nullptr;
    } else {
      if (auto stream = m_streams.tail()) {
        if (!stream->m_input_end) {
          stream->m_input_end = true;
          stream->output(evt);
        }
      }
    }

  } else if (evt->is<StreamEnd>()) {
    if (m_one_way_pipeline) {
      Pipeline::auto_release(m_one_way_pipeline);
      m_one_way_pipeline = nullptr;
    }
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
  auto p = demuxer->on_new_sub_pipeline(EventSource::reply());
  EventSource::chain(p->input());
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

  if (m_dedicated) {
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
      if (!demuxer->on_response_start(start)) {
        m_responses.push(new Response); // not the final response yet
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
// Demux::Options
//

Demux::Options::Options(pjs::Object *options) {
  Value(options, "outputCount")
    .get(output_count)
    .get(output_count_f)
    .check_nullable();
}

//
// Demux
//

Demux::Demux()
{
}

Demux::Demux(const Options &options)
  : m_options(options)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , m_options(r.m_options)
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

void Demux::chain() {
  Filter::chain();
  Demuxer::Queue::chain(Filter::output());
}

void Demux::reset() {
  Filter::reset();
  Demuxer::reset();
  Demuxer::Queue::reset();
}

void Demux::process(Event *evt) {
  Filter::output(evt, Demuxer::Queue::input());
}

void Demux::shutdown() {
  Filter::shutdown();
  Demuxer::Queue::shutdown();
}

auto Demux::on_queue_message(MessageStart *start) -> int {
  if (auto f = m_options.output_count_f.get()) {
    pjs::Value arg(start), ret;
    if (!Filter::callback(f, 1, &arg, ret)) return -1;
    return !ret.to_int32();
  } else {
    return m_options.output_count;
  }
}

auto Demux::on_open_stream() -> EventFunction* {
  return Demuxer::open_stream(
    Filter::sub_pipeline(0, true, nullptr)
  );
}

void Demux::on_close_stream(EventFunction *stream) {
  Demuxer::close_stream(stream);
}

} // namespace pipy
