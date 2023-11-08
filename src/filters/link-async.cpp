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

#include "link-async.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "input.hpp"
#include "net.hpp"

namespace pipy {

//
// LinkAsync
//

LinkAsync::LinkAsync(pjs::Function *name)
  : m_name_f(name)
  , m_buffer(Filter::buffer_stats())
{
}

LinkAsync::LinkAsync(const LinkAsync &r)
  : Filter(r)
  , m_name_f(r.m_name_f)
  , m_buffer(r.m_buffer)
{
}

LinkAsync::~LinkAsync()
{
}

void LinkAsync::dump(Dump &d) {
  Filter::dump(d);
  d.name = "linkAsync";
}

auto LinkAsync::clone() -> Filter* {
  return new LinkAsync(*this);
}

void LinkAsync::reset() {
  Filter::reset();
  EventSource::close();
  m_buffer.clear();
  m_pipeline = nullptr;
  if (m_async_wrapper) {
    m_async_wrapper->close();
    m_async_wrapper = nullptr;
  }
  m_is_started = false;
}

void LinkAsync::process(Event *evt) {
  if (!m_is_started) {
    if (m_name_f) {
      pjs::Value ret;
      if (!Filter::eval(m_name_f, ret)) return;
      if (ret.is_nullish()) return;

      if (ret.is_string()) {
        if (auto layout = module()->get_pipeline(ret.s())) {
          m_pipeline = sub_pipeline(layout, false, EventSource::reply())->start();
          m_is_started = true;
        } else if (auto aw = static_cast<JSModule*>(module())->alloc_pipeline_lb(ret.s(), Filter::output())) {
          m_async_wrapper = aw;
          m_is_started = true;
        } else {
          Filter::error("unknown pipeline layout name: %s", ret.s()->c_str());
          return;
        }

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
  } else if (auto p = m_pipeline.get()) {
    m_buffer.push(evt);
    Net::current().io_context().post(FlushHandler(this));
  } else if (auto aw = m_async_wrapper) {
    m_buffer.flush(
      [&](Event *evt) {
        aw->input(evt);
      }
    );
    aw->input(evt);
  }
}

void LinkAsync::on_reply(Event *evt) {
  Filter::output(evt);
}

void LinkAsync::flush() {
  if (!m_buffer.empty()) {
    InputContext ic;
    auto i = m_pipeline->input();
    m_buffer.flush(
      [&](Event *evt) {
        i->input(evt);
      }
    );
  }
}

} // namespace pipy
