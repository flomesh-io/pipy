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

#include "link.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "api/swap.hpp"

namespace pipy {

//
// Link
//

Link::Link(pjs::Function *name)
  : m_name_f(name)
{
}

Link::Link(const Link &r)
  : Filter(r)
  , m_name_f(r.m_name_f)
{
}

Link::~Link()
{
}

void Link::dump(Dump &d) {
  Filter::dump(d);
  d.name = "link";
}

auto Link::clone() -> Filter* {
  return new Link(*this);
}

void Link::reset() {
  Filter::reset();
  EventSource::close();
  m_buffer.clear();
  m_pipeline = nullptr;
  m_swap = nullptr;
  m_swap_input = nullptr;
  m_is_started = false;
}

void Link::process(Event *evt) {
  if (!m_is_started) {
    if (m_name_f) {
      pjs::Value ret;
      if (!Filter::eval(m_name_f, ret)) return;
      if (ret.is_nullish()) return;

      if (ret.is_string()) {
        if (auto layout = module()->get_pipeline(ret.s())) {
          m_pipeline = sub_pipeline(layout, false, EventSource::reply())->start();
          m_is_started = true;
        } else {
          Filter::error("unknown pipeline layout name: %s", ret.s()->c_str());
          return;
        }

      } else if (ret.is<Swap>()) {
        auto swap = ret.as<Swap>();
        auto input = swap->link(EventSource::reply());
        if (!input) {
          Filter::error("cannot link to a fully occupied Swap");
          return;
        }
        m_swap = swap;
        m_swap_input = input;
        m_is_started = true;

      } else {
        Filter::error("callback did not return a string");
        return;
      }

    } else if (Filter::num_sub_pipelines() > 0) {
      m_pipeline = sub_pipeline(0, false, EventSource::reply())->start();
      m_is_started = true;
    }
  }

  if (!m_is_started) {
    m_buffer.push(evt);

  } else if (auto i = input()) {
    if (m_is_outputting) {
      m_buffer.push(evt);
      Net::current().post(
        [this]() {
          if (auto i = input()) {
            flush(i);
          }
        }
      );
    } else {
      m_is_outputting = true;
      flush(i);
      i->input(evt);
      m_is_outputting = false;
    }
  }
}

void Link::on_reply(Event *evt) {
  Filter::output(evt);
}

auto Link::input() -> EventTarget::Input* {
  if (auto *p = m_pipeline.get()) {
    return p->input();
  } else {
    return m_swap_input.get();
  }
}

void Link::flush(EventTarget::Input *input) {
  if (!m_buffer.empty()) {
    m_buffer.flush(
      [=](Event *evt) {
        input->input(evt);
      }
    );
  }
}

} // namespace pipy
