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

#ifndef PIPELINE_LB_HPP
#define PIPELINE_LB_HPP

#include "event.hpp"
#include "net.hpp"
#include "pipeline.hpp"

#include <mutex>
#include <map>

namespace pipy {

//
// PipelineLoadBalancer
//

class PipelineLoadBalancer : public pjs::RefCountMT<PipelineLoadBalancer> {
public:
  static auto make() -> PipelineLoadBalancer* {
    return new PipelineLoadBalancer;
  }

  //
  // AsyncWrapper
  //

  class AsyncWrapper :
    public pjs::Pooled<AsyncWrapper>,
    public pjs::RefCountMT<AsyncWrapper>,
    public EventTarget {
  public:
    void input(Event *evt);
    void close();

  private:
    AsyncWrapper(Net *net, PipelineLayout *layout, EventTarget::Input *output);

    struct OpenHandler : SelfHandler<AsyncWrapper> {
      using SelfHandler::SelfHandler;
      OpenHandler(const OpenHandler &r) : SelfHandler(r) {}
      void operator()() { self->on_open(); }
    };

    struct CloseHandler : SelfHandler<AsyncWrapper> {
      using SelfHandler::SelfHandler;
      CloseHandler(const CloseHandler &r) : SelfHandler(r) {}
      void operator()() { self->on_close(); }
    };

    struct InputHandler : SelfDataHandler<AsyncWrapper, SharedEvent> {
      using SelfDataHandler::SelfDataHandler;
      InputHandler(AsyncWrapper *s, SharedEvent *d) : SelfDataHandler(s, d) { d->retain(); }
      InputHandler(const InputHandler &r) : SelfDataHandler(r) { r.data->retain(); }
      ~InputHandler() { data->release(); }
      void operator()() { self->on_input(data); }
    };

    struct OutputHandler : SelfDataHandler<AsyncWrapper, SharedEvent> {
      using SelfDataHandler::SelfDataHandler;
      OutputHandler(AsyncWrapper *s, SharedEvent *d) : SelfDataHandler(s, d) { d->retain(); }
      OutputHandler(const OutputHandler &r) : SelfDataHandler(r) { r.data->retain(); }
      ~OutputHandler() { data->release(); }
      void operator()() { self->on_output(data); }
    };

    virtual void on_event(Event *evt) override;

    void on_open();
    void on_close();
    void on_input(SharedEvent *se);
    void on_output(SharedEvent *se);

    Net* m_input_net;
    Net* m_output_net;
    pjs::Ref<PipelineLayout> m_pipeline_layout;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<EventTarget::Input> m_output;

    friend class pjs::RefCountMT<AsyncWrapper>;
    friend class PipelineLoadBalancer;
  };

  void add_target(PipelineLayout *target);
  auto allocate(const std::string &module, const std::string &name, EventTarget::Input *output) -> AsyncWrapper*;

private:
  PipelineLoadBalancer() {}
  ~PipelineLoadBalancer();

  //
  // PipelineLoadBalancer::Target
  //

  struct Target {
    Net* net = nullptr;
    Target* next = nullptr;
    pjs::Ref<PipelineLayout> layout;
  };

  //
  // PipelineLoadBalancer::PipelineInfo
  //

  struct PipelineInfo {
    Target* targets = nullptr;
    Target* current = nullptr;
    void add(Net *net, PipelineLayout *layout);
    auto next() -> Net*;
  };

  //
  // PipelineLoadBalancer::ModuleInfo
  //

  struct ModuleInfo {
    std::map<std::string, PipelineInfo> pipelines;
  };

  std::map<std::string, ModuleInfo> m_modules;
  std::mutex m_mutex;

  friend class pjs::RefCountMT<PipelineLoadBalancer>;
};

} // namespace pipy

#endif // PIPELINE_LB_HPP
