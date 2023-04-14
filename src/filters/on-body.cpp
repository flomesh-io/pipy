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

#include "on-body.hpp"
#include "context.hpp"

namespace pipy {

//
// OnBody
//

OnBody::OnBody(pjs::Function *callback, const Buffer::Options &options)
  : m_callback(callback)
  , m_data_buffer(options)
{
}

OnBody::OnBody(const OnBody &r)
  : Filter(r)
  , m_callback(r.m_callback)
  , m_data_buffer(r.m_data_buffer)
{
}

OnBody::~OnBody()
{
}

void OnBody::dump(Dump &d) {
  Filter::dump(d);
  d.name = "handleMessageBody";
}

auto OnBody::clone() -> Filter* {
  return new OnBody(*this);
}

void OnBody::reset() {
  Filter::reset();
  m_data_buffer.clear();
  m_event_buffer.clear();
  m_waiting = false;
  m_started = false;
}

void OnBody::process(Event *evt) {
  if (m_waiting) {
    m_event_buffer.push(evt);
  } else {
    handle(evt);
  }
}

void OnBody::handle(Event *evt) {
  if (evt->is<MessageStart>()) {
    m_started = true;

  } else if (auto data = evt->as<Data>()) {
    if (m_started) {
      m_data_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_started) {
      auto body = m_data_buffer.flush();
      pjs::Value arg(body), result;
      if (!Filter::callback(m_callback, 1, &arg, result)) return;
      if (result.is_promise()) {
        auto cb = Callback::make(this);
        result.as<pjs::Promise>()->then(context(), cb->resolved(), cb->rejected());
        m_waiting = true;
      }
      m_started = false;
    }
  }

  if (m_waiting) {
    m_event_buffer.push(evt);
  } else {
    Filter::output(evt);
  }
}

void OnBody::flush() {
  m_waiting = false;
  m_event_buffer.flush([this](Event *evt) { handle(evt); });
}

//
// OnBody::Callback
//

void OnBody::Callback::on_resolved(const pjs::Value &value) {
  m_filter->flush();
}

void OnBody::Callback::on_rejected(const pjs::Value &error) {
  if (error.is_error()) {
    m_filter->error(error.as<pjs::Error>());
  } else {
    m_filter->error(StreamEnd::make(error));
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<OnBody::Callback>::init() {
  super<Promise::Callback>();
}

} // namespace pjs
