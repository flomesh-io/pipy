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
// Demux
//

Demux::Demux(bool queued)
  : m_queued(queued)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , m_queued(r.m_queued)
{
}

Demux::~Demux() {
  clear_requests();
}

void Demux::dump(Dump &d) {
  Filter::dump(d);
  d.name = m_queued ? "demuxQueue" : "demux";
  d.sub_type = Dump::DEMUX;
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::reset() {
  Filter::reset();
  clear_requests();
  m_current_request = nullptr;
  m_current_response = nullptr;
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
        m_current_request = r;
        m_requests.push(r);
        r->retain();
        p->start();
        r->input(evt);
      }

    } else if (evt->is<Data>()) {
      if (m_started) {
        if (auto r = m_current_request.get()) {
          r->input(evt);
        }
      }

    } else if (evt->is_end()) {
      if (m_started) {
        m_started = false;
        if (auto r = m_current_request.get()) {
          r->input(evt->is<MessageEnd>() ? evt : MessageEnd::make());
          m_current_request = nullptr;
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
    auto request = r; r = r->next();
    m_requests.remove(request);
    request->release();
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
  auto output = demux->output();
  bool is_queued = demux->m_queued;
  bool is_current = false;

  if (is_queued) {
    is_current = (this == demux->m_requests.head());
  } else if (auto res = demux->m_current_response.get()) {
    is_current = (this == res);
  } else {
    demux->m_current_response = this;
    is_current = true;
    m_buffer.flush([=](Event *evt) { output->input(evt); });
  }

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
        if (is_queued) {
          if (is_current) {
            output->input(MessageStart::make());
            output->input(MessageEnd::make());
          } else {
            m_buffer.push(MessageStart::make());
            m_buffer.push(MessageEnd::make());
          }
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
      requests.remove(this);
      release();
      if (is_queued) {
        while (auto r = requests.head()) {
          r->m_buffer.flush([=](Event *evt) { output->input(evt); });
          if (!r->m_ended) break;
          requests.remove(r);
          r->release();
        }
      } else {
        for (auto r = requests.head(); r;) {
          auto request = r; r = r->next();
          if (request->m_ended) {
            requests.remove(request);
            request->m_buffer.flush([=](Event *evt) { output->input(evt); });
            request->release();
          }
        }
        demux->m_current_response = nullptr;
      }
    }
  }
}

} // namespace pipy
