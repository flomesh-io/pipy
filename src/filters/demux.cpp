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

namespace pipy {

//
// Demux::Options
//

Demux::Options::Options(pjs::Object *options) {
  Value(options, "messageKey")
    .get(message_key_f)
    .check_nullable();
}

//
// Demux
//

Demux::Demux(const Options &options)
  : m_options(options)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , m_options(r.m_options)
{
}

Demux::~Demux() {
  clear_requests();
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
  clear_requests();
  m_started = false;
  m_has_shutdown = false;
}

void Demux::process(Event *evt) {
  if (!m_has_shutdown) {
    if (evt->is<MessageStart>()) {
      if (!m_started) {
        m_started = true;
        auto p = Filter::sub_pipeline(0, true);
        auto r = new Request(this, p);
        m_requests.push(r);
        p->start();
        if (auto r = m_requests.tail()) {
          r->input(evt);
        }
      }

    } else if (evt->is<Data>()) {
      if (m_started) {
        if (auto r = m_requests.tail()) {
          r->input(evt);
        }
      }

    } else if (evt->is_end()) {
      if (m_started) {
        m_started = false;
        if (auto r = m_requests.tail()) {
          r->input(evt->is<MessageEnd>() ? evt : MessageEnd::make());
        }
      }
      if (evt->is<StreamEnd>()) {
        if (m_requests.empty()) {
          Filter::output(StreamEnd::make());
        } else {
          m_has_shutdown = true;
        }
      }
    }
  }
}

void Demux::clear_requests() {
  for (auto r = m_requests.head(); r;) {
    auto request = r;
    r = r->next();
    delete request;
  }
  m_requests.clear();
}

//
// Demux::Request
//

Demux::Request::Request(Demux *demux, Pipeline *pipeline)
  : m_demux(demux)
  , m_pipeline(pipeline)
{
  m_pipeline->chain(EventTarget::input());
}

void Demux::Request::input(Event *evt) {
  m_pipeline->input()->input(evt);
}

void Demux::Request::on_event(Event *evt) {
  auto demux = m_demux;
  bool is_current = (this == demux->m_requests.head());
  auto output = demux->output();

  if (evt->is<MessageStart>()) {
    if (!m_started) {
      m_started = true;
      if (is_current) {
        output->input(evt);
      } else {
        m_buffer.push(evt);
      }
    }

  } else if (evt->is<Data>()) {
    if (m_started && !m_ended) {
      if (is_current) {
        output->input(evt);
      } else {
        m_buffer.push(evt);
      }
    }

  } else if (evt->is_end()) {
    if (!m_started) {
      if (evt->is<StreamEnd>()) {
        m_started = true;
        m_ended = true;
        if (is_current) {
          output->input(MessageStart::make());
          output->input(MessageEnd::make());
        } else {
          m_buffer.push(MessageStart::make());
          m_buffer.push(MessageEnd::make());
        }
      }
    } else if (!m_ended) {
      m_ended = true;
      auto end = evt->as<MessageEnd>();
      if (!end) end = MessageEnd::make();
      if (is_current) {
        output->input(end);
      } else {
        m_buffer.push(end);
      }
    }
    if (is_current && m_ended) {
      auto &requests = demux->m_requests;
      while (auto r = requests.shift()) {
        delete static_cast<Request*>(r);
        auto h = requests.head();
        if (!h) break;
        h->m_buffer.flush([=](Event *evt) { output->input(evt); });
        if (!h->m_ended) break;
      }
    }
  }
}

} // namespace pipy
