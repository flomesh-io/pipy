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

#include <set>

#include "event.hpp"
#include "net.hpp"
#include "pipeline.hpp"
#include "pjs/pjs.hpp"
#include "timer.hpp"

namespace pipy {

class PipelineLayout;

class Task : public EventTarget {
 public:
  static auto make(const std::string &when, PipelineLayout *layout) -> Task * {
    return new Task(when, layout);
  }

  enum Type {
    ONE_SHOT,
    CRON,
    SIGNAL,
  };

  auto when() const -> const std::string & { return m_when; }
  auto type() const -> Type { return m_type; }
  auto interval() const -> int { return m_interval; }
  auto signal() const -> int { return m_signal; }
  auto pipeline_layout() const -> PipelineLayout * { return m_pipeline_layout; }
  auto pipeline() const -> Pipeline * { return m_pipeline; }
  bool active() const;
  void start();
  void end();

 private:
  Task(const std::string &when, PipelineLayout *layout);
  ~Task();

  std::string m_when;
  Type m_type = ONE_SHOT;
  double m_interval = 0;
  int m_signal = 0;
  Timer m_timer;
  asio::signal_set m_signal_set;
  pjs::Ref<PipelineLayout> m_pipeline_layout;
  pjs::Ref<Pipeline> m_pipeline;
  bool m_stream_end = false;

  void schedule(double interval);
  void keep_alive();
  void wait();
  void tick();
  void run();

  virtual void on_event(Event *evt) override;
};

}  // namespace pipy

#endif  // TASK_HPP
