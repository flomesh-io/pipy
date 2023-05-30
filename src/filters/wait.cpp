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

#include "wait.hpp"
#include "context.hpp"

namespace pipy {

//
// Wait::Options
//

Wait::Options::Options(pjs::Object *options) {
  Value(options, "timeout")
    .get(timeout)
    .check_nullable();
}

//
// Wait
//

Wait::Wait(pjs::Function *condition, const Options &options)
  : m_condition(condition)
  , m_options(options)
{
}

Wait::Wait(const Wait &r)
  : Filter(r)
  , m_condition(r.m_condition)
  , m_options(r.m_options)
{
}

Wait::~Wait()
{
}

void Wait::dump(Dump &d) {
  Filter::dump(d);
  d.name = "wait";
}

auto Wait::clone() -> Filter* {
  return new Wait(*this);
}

void Wait::reset() {
  Filter::reset();
  if (m_promise_callback) {
    m_promise_callback->close();
    m_promise_callback = nullptr;
  }
  m_timer.cancel();
  m_buffer.clear();
  m_fulfilled = false;
}

void Wait::process(Event *evt) {
  if (m_fulfilled) {
    output(evt);

  } else {
    pjs::Value ret;
    if (!callback(m_condition, 0, nullptr, ret)) return;
    if (!ret.is_promise()) {
      Filter::error("callback did not return a Promise");
      return;
    }

    auto cb = PromiseCallback::make(this);
    ret.as<pjs::Promise>()->then(context(), cb->resolved(), cb->rejected());
    m_promise_callback = cb;

    if (m_buffer.empty() && m_options.timeout > 0) {
      m_timer.schedule(
        m_options.timeout,
        [=]() { fulfill(); }
      );
    }
    m_buffer.push(evt);
  }
}

void Wait::fulfill() {
  if (!m_fulfilled) {
    m_timer.cancel();
    m_fulfilled = true;
    m_buffer.flush(
      [this](Event *evt) {
        output(evt);
      }
    );
  }
}

//
// Wait::PromiseCallback
//

void Wait::PromiseCallback::on_resolved(const pjs::Value &value) {
  if (m_filter) {
    m_filter->fulfill();
  }
}

void Wait::PromiseCallback::on_rejected(const pjs::Value &error) {
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

template<> void ClassDef<Wait::PromiseCallback>::init() {
  super<Promise::Callback>();
}

} // namespace pjs
