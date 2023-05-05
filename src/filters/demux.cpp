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
#include "net.hpp"

namespace pipy {

//
// Demuxer
//

void Demuxer::reset() {
  for (auto s = m_streams.head(); s; ) {
    auto stream = s; s = s->next();
    stream->end_input();
    stream->end_output();
  }
}

auto Demuxer::stream(Pipeline *pipeline) -> EventFunction* {
  auto s = new Stream(this);
  s->open(pipeline);
  return s;
}

//
// Demuxer::Stream
//
// A stream in demuxer is freed after both of the following methods have been called:
//   - end_input()
//   - end_output()
//

void Demuxer::Stream::open(Pipeline *pipeline) {
  m_pipeline = pipeline;
  EventProxy::chain_forward(pipeline->input());
  pipeline->chain(EventProxy::reply());
  pipeline->start();
}

void Demuxer::Stream::end_input() {
  if (!m_input_end) {
    m_input_end = true;
    recycle();
  }
}

void Demuxer::Stream::end_output() {
  if (!m_output_end) {
    m_output_end = true;
    recycle();
  }
}

void Demuxer::Stream::on_input(Event *evt) {
  if (!m_input_end) {
    bool is_end = evt->is<StreamEnd>();
    EventProxy::forward(evt);
    if (is_end) end_input();
  }
}

void Demuxer::Stream::on_reply(Event *evt) {
  if (!m_output_end) {
    bool is_end = evt->is<StreamEnd>();
    EventProxy::output(evt);
    if (is_end) end_output();
  }
}

//
// Demuxer::Queue
//

void Demuxer::Queue::reset() {
  clear();
  m_stream = nullptr;
  m_waiting_output_requested = false;
  m_waiting_output = false;
  m_dedicated_requested = false;
  m_dedicated = false;
  m_shutdown = false;
  m_closed = false;
}

void Demuxer::Queue::increase_output_count() {
  if (auto r = m_receivers.head()) {
    r->increase_output_count(1);
  }
}

void Demuxer::Queue::shutdown() {
  if (!m_closed) {
    if (m_receivers.empty()) {
      EventFunction::output(StreamEnd::make());
      close();
    } else {
      m_shutdown = true;
    }
  }
}

void Demuxer::Queue::on_event(Event *evt) {
  if (m_closed) return;

  if (m_dedicated) {
    m_stream->input()->input(evt);
    return;
  }

  while (!m_waiting_output && !m_buffer.empty()) {
    auto evt = m_buffer.shift();
    queue(evt);
    evt->release();
  }

  if (m_waiting_output) {
    m_buffer.push(evt);
    return;
  }

  queue(evt);
}

void Demuxer::Queue::queue(Event *evt) {
  switch (evt->type()) {
    case Event::Type::MessageStart:
      if (!m_stream) {
        int n = on_queue_message(evt->as<MessageStart>());
        if (n < 0) {
          m_waiting_output_requested = true;
          n = -n;
        }
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
      break;
    case Event::Type::Data:
      if (auto s = m_stream) {
        s->input()->input(evt);
      }
      break;
    case Event::Type::MessageEnd:
      if (auto s = m_stream) {
        if (m_waiting_output_requested) {
          m_waiting_output_requested = false;
          wait_output();
        }
        auto i = s->input();
        i->input(evt);
        if (!m_dedicated) {
          i->input(StreamEnd::make());
          m_stream = nullptr;
        }
      }
      break;
    case Event::Type::StreamEnd:
      if (auto s = m_stream) {
        auto i = s->input();
        if (!m_dedicated) {
          i->input(MessageEnd::make());
        }
        i->input(evt);
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

void Demuxer::Queue::wait_output() {
  if (!m_waiting_output) {
    m_waiting_output = true;
    if (auto *tap = InputContext::tap()) {
      tap->close();
      m_closed_tap = tap;
    }
  }
}

void Demuxer::Queue::continue_input() {
  if (m_waiting_output) {
    m_waiting_output = false;
    EventFunction::input()->flush_async();
    if (auto tap = m_closed_tap.get()) {
      tap->open();
      m_closed_tap = nullptr;
    }
  }
}

bool Demuxer::Queue::check_dedicated() {
  if (m_dedicated_requested) {
    if (m_stream) {
      m_stream->chain(EventFunction::output());
      if (m_stream_end) {
        auto evt = m_stream_end.release();
        auto inp = m_stream->input()->retain();
        Net::current().post(
          [=]() {
            InputContext ic;
            inp->input(evt);
            inp->release();
            evt->release();
          }
        );
      } else {
        continue_input();
      }
      clear();
      m_dedicated = true;
    }
  }
  return m_dedicated;
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
  if (m_receivers.empty()) {
    if (m_waiting_output) {
      if (!m_dedicated) {
        if (auto s = m_stream) {
          s->input()->input_async(StreamEnd::make());
          m_stream = nullptr;
        }
      }
      continue_input();
    }
    if (m_stream_end) {
      EventFunction::output(m_stream_end);
      close();
    } else if (m_shutdown) {
      EventFunction::output(StreamEnd::make());
      close();
    }
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
          if (!m_queue->check_dedicated()) {
            if (!--m_output_count) {
              m_queue->shift();
            }
          }
        }
        break;
      case Event::Type::StreamEnd:
        if (m_message_started) {
          m_queue->EventFunction::output(MessageEnd::make());
          m_message_started = false;
          if (!m_queue->check_dedicated()) {
            if (!--m_output_count) {
              m_queue->shift();
            } else {
              m_queue->EventFunction::output(evt);
              m_queue->close();
            }
          }
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
  return Demuxer::stream(
    Filter::sub_pipeline(0, true)
  );
}

} // namespace pipy
