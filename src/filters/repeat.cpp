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

#include "repeat.hpp"
#include "net.hpp"

namespace pipy {

//
// Repeat
//

Repeat::Repeat(pjs::Function *condition)
  : m_condition(condition)
  , m_buffer(Filter::buffer_stats())
{
}

Repeat::Repeat(const Repeat &r)
  : Filter(r)
  , m_condition(r.m_condition)
  , m_buffer(r.m_buffer)
{
}

Repeat::~Repeat()
{
}

void Repeat::dump(Dump &d) {
  Filter::dump(d);
  d.name = "repeat";
}

auto Repeat::clone() -> Filter* {
  return new Repeat(*this);
}

void Repeat::reset() {
  Filter::reset();
  EventSource::close();
  if (m_promise_cb) {
    m_promise_cb->discard();
    m_promise_cb = nullptr;
  }
  m_eos = nullptr;
  m_buffer.clear();
  m_timer.cancel();
  m_pipeline = nullptr;
  m_outputting = false;
  m_restarting = false;
  m_ended = false;
}

void Repeat::process(Event *evt) {
  if (m_ended) return;
  if (!m_pipeline) {
    auto p = sub_pipeline(0, false);
    m_pipeline = p;
    p->chain(EventSource::reply());
    p->start();
  }
  m_buffer.push(evt);
  m_pipeline->input()->input(evt);
}

void Repeat::on_reply(Event *evt) {
  if (auto eos = evt->as<StreamEnd>()) {
    m_eos = eos;
    pjs::Value arg(evt), ret;
    if (!Filter::callback(m_condition, 1, &arg, ret)) return;
    if (ret.is_promise()) {
      auto cb = pjs::Promise::Callback::make(
        [this](pjs::Promise::State state, const pjs::Value &value) {
          switch (state) {
            case pjs::Promise::RESOLVED:
              if (value.to_boolean()) {
                restart();
              } else {
                end();
              }
              break;
            case pjs::Promise::REJECTED:
              if (value.is_error()) {
                Filter::error(value.as<pjs::Error>());
              } else {
                Filter::error(StreamEnd::make(value));
              }
              break;
            default: break;
          }
        }
      );
      ret.as<pjs::Promise>()->then(nullptr, cb->resolved(), cb->rejected());
      m_promise_cb = cb;
    } else if (ret.to_boolean()) {
      restart();
    } else {
      end();
    }
  } else {
    m_outputting = true;
    Filter::output(evt);
    m_outputting = false;
  }
}

void Repeat::restart() {
  if (m_outputting) {
    if (!m_restarting) {
      m_restarting = true;
      m_timer.schedule(0, [this]() {
        m_restarting = false;
        repeat();
      });
    }
  } else {
    repeat();
  }
}

void Repeat::repeat() {
  auto p = sub_pipeline(0, false);
  auto i = p->input();
  m_pipeline = p;
  m_restarting = false;
  p->chain(EventSource::reply());
  p->start();
  m_buffer.iterate(
    [&](Event *evt) {
      i->input(evt->clone());
    }
  );
}

void Repeat::end() {
  m_ended = true;
  m_buffer.clear();
  Filter::output(m_eos);
}

} // namespace pipy
