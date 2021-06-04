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
#include "session.hpp"
#include "utils.hpp"

namespace pipy {

std::set<Task*> Task::s_all_tasks;

Task::Task(const std::string &interval, Pipeline *pipeline)
  : m_name(interval)
  , m_interval(utils::get_seconds(interval))
  , m_pipeline(pipeline)
{
  s_all_tasks.insert(this);
}

Task::~Task() {
  s_all_tasks.erase(this);
}

bool Task::active() const {
  return m_session && !m_session->done();
}

void Task::start() {
  schedule(0);
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
    if (!active()) {
      m_session = nullptr;
      auto ctx = m_pipeline->module()->worker()->new_runtime_context();
      m_session = Session::make(ctx, m_pipeline);
      m_session->input(MessageStart::make());
      m_session->input(MessageEnd::make());
    }
    schedule(m_interval);
  }
}

} // namespace pipy