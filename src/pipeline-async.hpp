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

#ifndef PIPELINE_ASYNC_HPP
#define PIPELINE_ASYNC_HPP

#include "event.hpp"
#include "event-queue.hpp"
#include "net.hpp"
#include "pipeline.hpp"

#include <atomic>
#include <mutex>

namespace pipy {

//
// PipelineAsyncWrapper
//

class PipelineAsyncWrapper : public EventTarget {
public:
  static void register_pipeline_layout(PipelineLayout *layout);
  static void unregister_all_pipeline_layouts();

  static auto make(const std::string &name, EventTarget::Input *output) -> PipelineAsyncWrapper* {
    return new PipelineAsyncWrapper(name, output);
  }

  auto retain() -> PipelineAsyncWrapper* { m_refs.fetch_add(1, std::memory_order_relaxed); return this; }
  void release() { if (m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }

  void input(Event *evt);
  void close();

private:

  //
  // PipelineAsyncWrapper::PipelineOwner
  //

  struct PipelineOwner {
    Net* net = nullptr;
    pjs::Ref<PipelineLayout> layout;
    PipelineOwner* next = nullptr;
  };

  //
  // PipelineAsyncWrapper::PipelineEntry
  //

  struct PipelineEntry {
    PipelineOwner* owners = nullptr;
    PipelineOwner* current = nullptr;
    void add(Net *net, PipelineLayout *layout);
    void remove(Net *net);
    auto next() -> Net*;
  };

  //
  // Handlers
  //

  struct OpenHandler : SelfHandler<PipelineAsyncWrapper> {
    using SelfHandler::SelfHandler;
    OpenHandler(const OpenHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_open(); }
  };

  struct CloseHandler : SelfHandler<PipelineAsyncWrapper> {
    using SelfHandler::SelfHandler;
    CloseHandler(const CloseHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_close(); }
  };

  struct InputHandler : SelfHandler<PipelineAsyncWrapper> {
    using SelfHandler::SelfHandler;
    InputHandler(const InputHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_input(); }
  };

  struct OutputHandler : SelfHandler<PipelineAsyncWrapper> {
    using SelfHandler::SelfHandler;
    OutputHandler(const OutputHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_input(); }
  };

  PipelineAsyncWrapper(const std::string &name, EventTarget::Input *output);
  ~PipelineAsyncWrapper() {}

  virtual void on_event(Event *evt) override;

  void on_open();
  void on_close();
  void on_input();
  void on_output();

  std::atomic<int> m_refs;
  EventQueue m_input_queue;
  EventQueue m_output_queue;
  Net* m_input_net = nullptr;
  Net* m_output_net;
  pjs::Ref<PipelineLayout> m_pipeline_layout;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<EventTarget::Input> m_output;

  static std::map<std::string, PipelineEntry> m_registry;
  static std::mutex m_registry_mutex;
};

} // namespace pipy

#endif // PIPELINE_ASYNC_HPP
