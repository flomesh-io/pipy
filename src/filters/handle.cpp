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

#include "handle.hpp"
#include "context.hpp"

namespace pipy {

//
// Handle
//

Handle::Handle(pjs::Function *callback)
  : m_callback(callback)
{
}

Handle::Handle(const Handle &r)
  : Filter(r)
  , m_callback(r.m_callback)
{
}

Handle::~Handle()
{
}

void Handle::reset() {
  Filter::reset();
  m_event_buffer.clear();
  if (m_promise_callback) {
    m_promise_callback->close();
    m_promise_callback = nullptr;
  }
  m_promise = nullptr;
  m_waiting = false;
}

void Handle::process(Event *evt) {
  if (m_waiting) {
    m_event_buffer.push(evt);
  } else {
    handle(evt);
  }
}

bool Handle::callback(pjs::Object *arg) {
  pjs::Value a(arg), result;
  if (!Filter::callback(m_callback, 1, &a, result)) return false;
  if (result.is_promise()) {
    auto cb = PromiseCallback::make(this);
    m_promise = result.as<pjs::Promise>()->then(context(), cb->resolved(), cb->rejected());
    m_promise_callback = cb;
    m_waiting = true;
    return true;
  } else {
    return on_callback_return(result);
  }
}

bool Handle::on_callback_return(const pjs::Value &result) {
  m_waiting = false;
  if (m_deferred_event) {
    Filter::output(m_deferred_event);
    m_deferred_event = nullptr;
  }
  m_event_buffer.flush([this](Event *evt) {
    handle(evt);
  });
  return true;
}

void Handle::defer(Event *evt) {
  if (m_waiting) {
    m_deferred_event = evt;
  } else {
    Filter::output(evt);
  }
}

void Handle::pass(Event *evt) {
  if (m_waiting) {
    m_event_buffer.push(evt);
  } else {
    Filter::output(evt);
  }
}

//
// Handle::PromiseCallback
//

void Handle::PromiseCallback::on_resolved(const pjs::Value &value) {
  if (m_filter) {
    m_filter->on_callback_return(value);
  }
}

void Handle::PromiseCallback::on_rejected(const pjs::Value &error) {
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

template<> void ClassDef<Handle::PromiseCallback>::init() {
  super<Promise::Callback>();
}

} // namespace pjs
