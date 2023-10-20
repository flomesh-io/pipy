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

#include "pipeline-async.hpp"
#include "module.hpp"

namespace pipy {

std::map<std::string, PipelineAsyncWrapper::PipelineEntry> PipelineAsyncWrapper::m_registry;
std::mutex PipelineAsyncWrapper::m_registry_mutex;

void PipelineAsyncWrapper::register_pipeline_layout(PipelineLayout *layout) {
  std::lock_guard<std::mutex> lock(m_registry_mutex);
  auto *o = new PipelineOwner;
  auto &p = m_registry[layout->name()->str()];
  o->net = &Net::current();
  o->next = p.owners;
  p.owners = o;
}

void PipelineAsyncWrapper::unregister_all_pipeline_layouts() {
  std::lock_guard<std::mutex> lock(m_registry_mutex);
  auto net = &Net::current();
  for (auto &p : m_registry) {
    PipelineOwner *b = nullptr;
    for (auto o = p.second.owners; o; o = o->next) {
      if (o->net == net) {
        auto n = o->next;
        delete o;
        if (p.second.current == o) {
          p.second.current = n;
        }
        if (b) {
          b->next = n;
        } else {
          p.second.owners = n;
        }
        break;
      }
    }
  }
}

PipelineAsyncWrapper::PipelineAsyncWrapper(const std::string &name, EventTarget::Input *output)
  : m_output_net(&Net::current())
  , m_output(output)
{
  std::lock_guard<std::mutex> lock(m_registry_mutex);
  auto i = m_registry.find(name);
  if (i != m_registry.end()) {
    auto o = i->second.current;
    if (!o) o = i->second.owners;
    if (!o) return;
    m_input_net = o->net;
    m_pipeline_layout = o->layout;
    i->second.current = o->next;
    o->net->io_context().post(OpenHandler(this));
  }
}

void PipelineAsyncWrapper::input(Event *evt) {
  if (m_input_net) {
    m_input_queue.enqueue(evt);
    m_input_net->io_context().post(InputHandler(this));
  }
}

void PipelineAsyncWrapper::close() {
  if (m_input_net) {
    m_input_net->io_context().post(CloseHandler(this));
  }
}

void PipelineAsyncWrapper::on_event(Event *evt) {
  if (m_output_net) {
    m_output_queue.enqueue(evt);
    m_output_net->io_context().post(OutputHandler(this));
  }
}

void PipelineAsyncWrapper::on_open() {
  if (m_pipeline_layout && !m_pipeline) {
    auto mod = m_pipeline_layout->module();
    m_pipeline = Pipeline::make(m_pipeline_layout, mod->new_context());
    m_pipeline->chain(EventTarget::input());
  }
}

void PipelineAsyncWrapper::on_close() {
  m_pipeline = nullptr;
  m_pipeline_layout = nullptr;
  m_output = nullptr;
}

void PipelineAsyncWrapper::on_input() {
  if (auto evt = m_input_queue.dequeue()) {
    if (m_pipeline) {
      m_pipeline->input()->input(evt);
    } else {
      evt->retain();
      evt->release();
    }
  }
}

void PipelineAsyncWrapper::on_output() {
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
