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

#ifndef TASK_HPP
#define TASK_HPP

#include "pjs/pjs.hpp"
#include "event.hpp"
#include "pipeline.hpp"
#include "timer.hpp"

#include <set>

namespace pipy {

class PipelineDef;

class Task : public EventTarget {
public:
  static auto make(const std::string &interval, PipelineDef *pipeline_def) -> Task* {
    return new Task(interval, pipeline_def);
  }

  auto name() const -> const std::string& { return m_name; }
  auto pipeline_def() const -> PipelineDef* { return m_pipeline_def; }
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  bool active() const;
  bool start();

private:
  Task(const std::string &interval, PipelineDef *pipeline_def);
  ~Task();

  std::string m_name;
  double m_interval;
  pjs::Ref<PipelineDef> m_pipeline_def;
  pjs::Ref<Pipeline> m_pipeline;
  bool m_stopped = false;
  Timer m_timer;

  void schedule(double interval);
  void tick();
  void run();
  void stop();

  virtual void on_event(Event *evt) override;

  friend class Worker;
};

} // namespace pipy

#endif // TASK_HPP
