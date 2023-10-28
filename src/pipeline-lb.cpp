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

#include "pipeline-lb.hpp"
#include "module.hpp"
#include "input.hpp"

namespace pipy {

PipelineLoadBalancer::~PipelineLoadBalancer() {
  for (const auto &m : m_modules) {
    for (const auto &p : m.second.pipelines) {
      auto t = p.second.targets;
      while (t) {
        auto target = t; t = t->next;
        delete target;
      }
    }
  }
}

void PipelineLoadBalancer::add_target(PipelineLayout *layout) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto *m = static_cast<Module*>(layout->module());
  auto &p = m_modules[m->filename()->str()].pipelines[layout->name()->str()];
  auto *t = new Target;
  t->net = &Net::current();
  t->next = p.targets;
  p.targets = t;
}

auto PipelineLoadBalancer::allocate(const std::string &module, const std::string &name, EventTarget::Input *output) -> AsyncWrapper* {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto i = m_modules.find(module); if (i == m_modules.end()) return nullptr;
  auto j = i->second.pipelines.find(name); if (j == i->second.pipelines.end()) return nullptr;
  auto t = j->second.current;
  if (!t) t = j->second.targets;
  if (!t) return nullptr;
  j->second.current = t->next;
  return new AsyncWrapper(t->net, t->layout, output);
}

//
// PipelineLoadBalancer::AsyncWrapper
//

PipelineLoadBalancer::AsyncWrapper::AsyncWrapper(Net *net, PipelineLayout *layout, EventTarget::Input *output)
  : m_input_net(net)
  , m_output_net(&Net::current())
  , m_pipeline_layout(layout)
  , m_output(output)
{
}

void PipelineLoadBalancer::AsyncWrapper::input(Event *evt) {
  if (m_input_net) {
    m_input_queue.enqueue(evt);
    m_input_net->io_context().post(InputHandler(this));
  }
}

void PipelineLoadBalancer::AsyncWrapper::close() {
  if (m_input_net) {
    m_input_net->io_context().post(CloseHandler(this));
  }
}

void PipelineLoadBalancer::AsyncWrapper::on_event(Event *evt) {
  if (m_output_net) {
    m_output_queue.enqueue(evt);
    m_output_net->io_context().post(OutputHandler(this));
  }
}

void PipelineLoadBalancer::AsyncWrapper::on_open() {
  if (m_pipeline_layout && !m_pipeline) {
    auto mod = m_pipeline_layout->module();
    m_pipeline = Pipeline::make(m_pipeline_layout, mod->new_context());
    m_pipeline->chain(EventTarget::input());
  }
}

void PipelineLoadBalancer::AsyncWrapper::on_close() {
  m_pipeline = nullptr;
  m_pipeline_layout = nullptr;
  m_output = nullptr;
}

void PipelineLoadBalancer::AsyncWrapper::on_input() {
  if (auto evt = m_input_queue.dequeue()) {
    if (m_pipeline) {
      InputContext ic;
      m_pipeline->input()->input(evt);
    } else {
      evt->retain();
      evt->release();
    }
  }
}

void PipelineLoadBalancer::AsyncWrapper::on_output() {
  if (auto evt = m_output_queue.dequeue()) {
    if (m_output) {
      m_output->input(evt);
    } else {
      evt->retain();
      evt->release();
    }
  }
}

} // namespace pipy
