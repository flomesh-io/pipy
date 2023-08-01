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

#include "loop.hpp"
#include "pipeline.hpp"
#include "input.hpp"

namespace pipy {

//
// Loop
//

Loop::Loop()
  : m_buffer(Filter::buffer_stats())
{
}

Loop::Loop(const Loop &r)
  : Filter(r)
  , m_buffer(r.m_buffer)
{
}

Loop::~Loop()
{
}

void Loop::dump(Dump &d) {
  Filter::dump(d);
  d.name = "loop";
}

auto Loop::clone() -> Filter* {
  return new Loop(*this);
}

void Loop::reset() {
  Filter::reset();
  EventSource::close();
  m_buffer.clear();
  m_pipeline = nullptr;
  if (m_flush_task) {
    m_flush_task->cancel();
    m_flush_task = nullptr;
  }
}

void Loop::process(Event *evt) {
  if (!m_pipeline) {
    m_pipeline = Filter::sub_pipeline(0, false, EventSource::reply());
    m_pipeline->start();
  }

  m_is_outputting = true;
  m_pipeline->input()->input(evt);
  m_is_outputting = false;
}

void Loop::on_reply(Event *evt) {
  if (m_is_outputting) {
    m_buffer.push(evt);
    if (!m_flush_task) {
      m_flush_task = new FlushTask(this);
    }
  } else {
    Filter::output(evt);
    m_is_outputting = true;
    m_pipeline->input()->input(evt);
    m_is_outputting = false;
  }
}

void Loop::flush() {
  InputContext ic;
  EventBuffer events(std::move(m_buffer));
  auto *i = m_pipeline->input();
  events.flush(
    [&](Event *evt) {
      Filter::output(evt);
      i->input(evt);
    }
  );
}

} // namespace pipy
