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

#ifndef LINK_ASYNC_HPP
#define LINK_ASYNC_HPP

#include "filter.hpp"
#include "pipeline-lb.hpp"
#include "net.hpp"

namespace pipy {

//
// LinkAsync
//

class LinkAsync : public Filter, public EventSource {
public:
  LinkAsync(pjs::Function *name = nullptr);

private:
  LinkAsync(const LinkAsync &r);
  ~LinkAsync();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void dump(Dump &d) override;

  struct FlushHandler : SelfHandler<LinkAsync> {
    using SelfHandler::SelfHandler;
    FlushHandler(const FlushHandler &r) : SelfHandler(r) {}
    void operator()() { self->flush(); }
  };

  pjs::Ref<pjs::Function> m_name_f;
  pjs::Ref<Pipeline> m_pipeline;
  PipelineLoadBalancer::AsyncWrapper* m_async_wrapper = nullptr;
  EventBuffer m_buffer;
  bool m_is_started = false;

  void flush();
};

} // namespace pipy

#endif // LINK_ASYNC_HPP
