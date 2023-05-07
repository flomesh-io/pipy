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
// DemuxQueue
//
// Isolates messages from its input into separate streams.
// Each stream is alive until both its input and output are closed.
// Stream input is closed at an input event of type MessageEnd or StreamEnd.
// Stream output is closed when anticipated output count is reached or StreamEnd.
// All streams are closed at DemuxQueue::reset().
//

void DemuxQueue::reset() {
  clear_receivers();
  clear_waiters();
  if (auto *s = m_input_stream) {
    close_stream(s);
    m_input_stream = nullptr;
  }
  if (auto tap = m_closed_tap.get()) {
    tap->open();
    m_closed_tap = nullptr;
  }
  m_stream_count = 0;
  m_output_count = 1;
  m_waiting_output_requested = false;
  m_waiting_output = false;
  m_dedication_requested = false;
  m_dedicated = false;
}

void DemuxQueue::on_event(Event *evt) {
  if (m_dedicated) {
    if (auto s = m_input_stream) {
      auto i = s->handler->input();
      while (!m_buffer.empty()) {
        auto evt = m_buffer.shift();
        i->input(evt);
        evt->release();
      }
      i->input(evt);
    }

  } else {
    while (!m_waiting_output && !m_buffer.empty()) {
      auto evt = m_buffer.shift();
      queue_event(evt);
      evt->release();
    }

    if (m_waiting_output) {
      m_buffer.push(evt);
      return;
    }

    queue_event(evt);
  }
}

void DemuxQueue::queue_event(Event *evt) {
  switch (evt->type()) {
    case Event::Type::MessageStart:
      if (!m_input_stream) {
        auto s = m_input_stream = open_stream();
        auto n = m_output_count;
        auto h = s->handler;
        if (n > 0) {
          auto r = new Receiver(this, s, n);
          h->chain(r->input());
          m_receivers.push(r);
        } else {
          auto w = new Waiter(this, s);
          h->chain(w->input());
          m_waiters.push(w);
        }
        h->input()->input(evt);
      }
      break;
    case Event::Type::Data:
      if (auto s = m_input_stream) {
        if (!Data::is_flush(evt)) {
          s->handler->input()->input(evt);
        }
      }
      break;
    case Event::Type::MessageEnd:
    case Event::Type::StreamEnd:
      if (auto s = m_input_stream) {
        if (m_waiting_output_requested) {
          m_waiting_output_requested = false;
          start_waiting_output();
        }
        s->handler->input()->input(evt); // might've turned dedicated after
        if (!m_dedicated) {
          close_stream_input(s);
          m_input_stream = nullptr;
        }
      }
      break;
  }
}

void DemuxQueue::start_waiting_output() {
  if (!m_waiting_output) {
    m_waiting_output = true;
    if (auto tap = InputContext::tap()) {
      tap->close();
      m_closed_tap = tap;
    }
  }
}

void DemuxQueue::continue_input() {
  if (m_waiting_output) {
    m_waiting_output = false;
    EventFunction::input()->flush_async();
    if (auto tap = m_closed_tap.get()) {
      tap->open();
      m_closed_tap = nullptr;
    }
  }
}

bool DemuxQueue::check_dedicated() {
  if (m_dedication_requested && !m_dedicated) {
    if (auto r = m_receivers.head()) {
      auto s = r->stream();
      m_receivers.remove(r);
      delete r;
      if (s != m_input_stream) {
        close_stream_input(m_input_stream);
        m_input_stream = s;
      }
      if (s) {
        s->end_input = true; // reopen input if it's closed already
        s->handler->chain(EventFunction::output());
      }
      continue_input();
      clear_receivers();
    }
    return (m_dedicated = true);
  }
  return false;
}

void DemuxQueue::shift_receiver() {
  auto r = m_receivers.head();
  close_stream_output(r->stream());
  m_receivers.remove(r);
  delete r;
  while (auto r = m_receivers.head()) {
    if (!r->flush()) break;
    close_stream_output(r->stream());
    m_receivers.remove(r);
    delete r;
  }
  if (m_receivers.empty()) continue_input();
}

void DemuxQueue::clear_receivers() {
  while (auto r = m_receivers.head()) {
    close_stream_output(r->stream());
    m_receivers.remove(r);
    delete r;
  }
}

void DemuxQueue::clear_waiters() {
  while (auto w = m_waiters.head()) {
    close_stream_output(w->stream());
    m_waiters.remove(w);
    delete w;
  }
}

//
// DemuxQueue::Receiver
//

bool DemuxQueue::Receiver::flush() {
  m_buffer.flush(
    [this](Message *msg) {
      msg->write(m_queue->EventFunction::output());
    }
  );
  return !m_output_count;
}

void DemuxQueue::Receiver::on_event(Event *evt) {
  auto q = m_queue;
  if (q->m_receivers.head() == this) {
    switch (evt->type()) {
      case Event::Type::MessageStart: {
        if (!m_has_message_started) {
          q->EventFunction::output(evt);
          m_has_message_started = true;
        }
        break;
      }
      case Event::Type::Data:
        if (m_has_message_started) {
          q->EventFunction::output(evt);
        }
        break;
      case Event::Type::MessageEnd:
        if (m_has_message_started) {
          q->EventFunction::output(evt);
          m_has_message_started = false;
          if (!q->check_dedicated()) {
            if (!--m_output_count) {
              q->shift_receiver();
            }
          }
        }
        break;
      case Event::Type::StreamEnd:
        q->close_stream_output(m_stream);
        m_stream = nullptr;
        if (m_has_message_started) {
          q->EventFunction::output(MessageEnd::make());
          m_has_message_started = false;
          if (!--m_output_count) {
            q->shift_receiver();
          } else { // Short of output count, abort
            q->EventFunction::output(evt);
            q->reset();
          }
        }
        break;
    }
  } else if (m_output_count > 0) {
    if (auto msg = m_reader.read(evt)) {
      m_buffer.push(msg);
      if (!--m_output_count) {
        q->close_stream_output(m_stream);
        m_stream = nullptr;
      }
      msg->release();
    }
  }
}

//
// DemuxQueue::Waiter
//

void DemuxQueue::Waiter::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_queue->close_stream_output(m_stream);
    m_stream = nullptr;
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
  DemuxQueue::chain(Filter::output());
}

void Demux::reset() {
  Filter::reset();
  DemuxQueue::reset();
  m_eos = nullptr;
}

void Demux::process(Event *evt) {
  if (auto ms = evt->as<MessageStart>()) {
    auto n = 1;

    if (auto f = m_options.output_count_f.get()) {
      pjs::Value arg(ms), ret;
      if (Filter::callback(f, 1, &arg, ret)) {
        n = ret.to_int32();
      }
    } else {
      n = m_options.output_count;
    }

    if (n >= 0) {
      DemuxQueue::output_count(n);
    } else {
      DemuxQueue::output_count(-n);
      DemuxQueue::wait_output();
    }

    DemuxQueue::input()->input(evt);

  } else {
    DemuxQueue::input()->input(evt);

    if (auto eos = evt->as<StreamEnd>()) {
      if (DemuxQueue::stream_count() > 0) {
        m_eos = eos;
      } else {
        Filter::output(evt);
      }
    }
  }
}

auto Demux::on_demux_open_stream() -> EventFunction* {
  auto p = Filter::sub_pipeline(0, true);
  p->retain();
  return p;
}

void Demux::on_demux_close_stream(EventFunction *stream) {
  auto p = static_cast<Pipeline*>(stream);
  p->release();
}

void Demux::on_demux_complete() {
  if (auto eos = m_eos) {
    Filter::output(eos);
    m_eos = nullptr;
  }
}

} // namespace pipy
