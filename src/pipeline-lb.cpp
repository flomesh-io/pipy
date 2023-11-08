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
        auto pl = t->layout.release();
        t->net->post(
          [=]() {
            pl->release();
          }
        );
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
  t->layout = layout;
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
  retain();
  m_input_net->io_context().post(OpenHandler(this));
}

void PipelineLoadBalancer::AsyncWrapper::input(Event *evt) {
  retain();
  m_input_net->io_context().post(InputHandler(this, SharedEvent::make(evt)));
}

void PipelineLoadBalancer::AsyncWrapper::close() {
  m_output = nullptr;
  m_input_net->io_context().post(CloseHandler(this));
}

void PipelineLoadBalancer::AsyncWrapper::on_event(Event *evt) {
  retain();
  m_output_net->io_context().post(OutputHandler(this, SharedEvent::make(evt)));
}

void PipelineLoadBalancer::AsyncWrapper::on_open() {
  InputContext ic;
  auto mod = m_pipeline_layout->module();
  m_pipeline = Pipeline::make(m_pipeline_layout, mod->new_context());
  m_pipeline->chain(EventTarget::input());
  m_pipeline->start();
}

void PipelineLoadBalancer::AsyncWrapper::on_close() {
  m_pipeline = nullptr;
  m_pipeline_layout = nullptr;
  EventTarget::close();
  release();
}

void PipelineLoadBalancer::AsyncWrapper::on_input(SharedEvent *se) {
  if (auto evt = se->to_event()) {
    if (m_pipeline) {
      InputContext ic;
      m_pipeline->input()->input(evt);
    } else {
      evt->retain();
      evt->release();
    }
  }
  release();
}

void PipelineLoadBalancer::AsyncWrapper::on_output(SharedEvent *se) {
  if (auto evt = se->to_event()) {
    if (m_output) {
      InputContext ic;
      m_output->input(evt);
    } else {
      evt->retain();
      evt->release();
    }
  }
  release();
}

} // namespace pipy
