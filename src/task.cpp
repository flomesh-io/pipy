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

#include "task.hpp"
#include "listener.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "worker.hpp"
#include "utils.hpp"

namespace pipy {

Task::Task(const std::string &interval, PipelineDef *pipeline_def)
  : m_name(interval)
  , m_interval(utils::get_seconds(interval))
  , m_pipeline_def(pipeline_def)
{
}

Task::~Task() {
}

bool Task::active() const {
  return m_pipeline;
}

bool Task::start() {
  if (m_name.empty()) {
    run();
    return true;
  } else {
    schedule(0);
    return true;
  }
}

void Task::stop() {
  m_stopped = true;
  m_timer.cancel();
  schedule(0);
}

void Task::schedule(double interval) {
  m_timer.schedule(
    interval,
    [this]() {
      tick();
    }
  );
}

void Task::tick() {
  if (m_stopped) {
    if (active()) {
      schedule(1);
    } else {
      delete this;
    }
  } else {
    if (!active()) run();
    schedule(m_interval);
  }
}

void Task::run() {
  m_pipeline = Pipeline::make(
    m_pipeline_def,
    m_pipeline_def->module()->worker()->new_runtime_context()
  );
  m_pipeline->chain(EventTarget::input());
  Pipeline::AutoReleasePool arp;
  auto input = m_pipeline->input();
  input->input(MessageStart::make());
  input->input(MessageEnd::make());
}

void Task::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_pipeline = nullptr;
  }
}

} // namespace pipy
