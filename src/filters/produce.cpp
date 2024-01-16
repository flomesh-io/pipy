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

#include "produce.hpp"
#include "message.hpp"

namespace pipy {

//
// Produce
//

Produce::Produce(const pjs::Value &producer)
  : m_producer(producer)
{
}

Produce::Produce(const Produce &r)
  : m_producer(r.m_producer)
{
}

Produce::~Produce()
{
}

void Produce::dump(Dump &d) {
  Filter::dump(d);
  d.name = "produce";
}

auto Produce::clone() -> Filter* {
  return new Produce(*this);
}

void Produce::reset() {
  Filter::reset();
  if (m_promise_callback) {
    m_promise_callback->close();
    m_promise_callback = nullptr;
  }
  m_started = false;
}

void Produce::process(Event *evt) {
  if (!m_started) {
    m_started = true;
    produce();
  }
}

void Produce::produce() {
  do {
    pjs::Value events;
    if (!Filter::eval(m_producer, events)) break;
    if (events.is_promise()) {
      auto cb = PromiseCallback::make(this);
      events.as<pjs::Promise>()->then(nullptr, cb->resolved(), cb->rejected());
      m_promise_callback = cb;
      break;
    }
    if (!consume(events)) break;
  } while (m_producer.is_function());
}

bool Produce::consume(const pjs::Value &value) {
  bool ended = false;
  if (Message::to_events(
    value, [&](Event *evt) {
      if (evt->is<StreamEnd>()) {
        Filter::output(evt);
        ended = true;
        return false;
      }
      Filter::output(evt);
      return true;
    }
  )) return true;
  if (!ended) Filter::error("production is not an event");
  return false;
}

void Produce::fulfill(const pjs::Value &value) {
  if (!consume(value)) return;
  if (m_producer.is_function()) {
    produce();
  }
}

//
// Produce::PromiseCallback
//

void Produce::PromiseCallback::on_resolved(const pjs::Value &value) {
  if (m_filter) {
    m_filter->fulfill(value);
  }
}

void Produce::PromiseCallback::on_rejected(const pjs::Value &error) {
  if (m_filter) {
    if (error.is_error()) {
      m_filter->error(error.as<pjs::Error>());
    } else {
      m_filter->error(StreamEnd::make(error));
    }
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Produce::PromiseCallback>::init() {
  super<Promise::Callback>();
}

} // namespace pjs
