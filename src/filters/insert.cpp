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

#include "insert.hpp"
#include "message.hpp"

namespace pipy {

//
// Insert
//

Insert::Insert(pjs::Object *events)
  : m_events(events)
{
}

Insert::Insert(const Insert &r)
  : Filter(r)
  , m_events(r.m_events)
{
}

Insert::~Insert()
{
}

void Insert::dump(Dump &d) {
  Filter::dump(d);
  d.name = "insert";
}

auto Insert::clone() -> Filter* {
  return new Insert(*this);
}

void Insert::reset() {
  Filter::reset();
  m_inserted = false;
  if (m_promise_callback) {
    m_promise_callback->close();
    m_promise_callback = nullptr;
  }
}

void Insert::process(Event *evt) {
  if (!m_inserted) {
    m_inserted = true;
    pjs::Value events(m_events.get());
    if (m_events && m_events->is<pjs::Function>()) {
      if (!Filter::eval(m_events->as<pjs::Function>(), events)) return;
    }
    if (events.is_promise()) {
      auto cb = PromiseCallback::make(this);
      events.as<pjs::Promise>()->then(nullptr, cb->resolved(), cb->rejected());
      m_promise_callback = cb;
    } else if (!Message::output(events, Filter::output())) {
      Filter::error("inserting object is not an event or Message or an array of those");
    }
  }
  Filter::output(evt);
}

void Insert::on_callback_return(const pjs::Value &result) {
  if (!Message::output(result, Filter::output())) {
    Filter::error("Promise was not fulfilled with an event or Message or an array of those");
  }
}

//
// Insert::PromiseCallback
//

void Insert::PromiseCallback::on_resolved(const pjs::Value &value) {
  if (m_filter) {
    m_filter->on_callback_return(value);
  }
}

void Insert::PromiseCallback::on_rejected(const pjs::Value &error) {
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

template<> void ClassDef<Insert::PromiseCallback>::init() {
  super<Promise::Callback>();
}

} // namespace pjs
