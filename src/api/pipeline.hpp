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

#ifndef API_PIPELINE_HPP
#define API_PIPELINE_HPP

#include "net.hpp"
#include "pipeline.hpp"
#include "filter.hpp"

namespace pipy {

//
// PipelineDesigner
//

class PipelineDesigner : public pjs::ObjectTemplate<PipelineDesigner> {
public:
  static auto make_pipeline_layout(
    pjs::Context &ctx,
    pjs::Function *builder
  ) -> PipelineLayout*;

  auto trace_location(pjs::Context &ctx) -> PipelineDesigner*;

  void on_start(pjs::Object *starting_events);
  void on_end(pjs::Function *handler);

  void to(pjs::Str *name);
  void to(PipelineLayout *layout);

  void connect(const pjs::Value &target, pjs::Object *options);
  void demux_http(pjs::Object *options);
  void dummy();
  void dump(const pjs::Value &tag);
  void link(pjs::Str *name);
  void link(pjs::Function *func);
  void mux_http(pjs::Function *session_selector, pjs::Object *options);

  void close();

private:
  PipelineDesigner(PipelineLayout *layout)
    : m_layout(layout) {}

  void check_integrity();
  auto append_filter(Filter *filter) -> Filter*;
  void require_sub_pipeline(Filter *filter);

  PipelineLayout* m_layout;
  Filter* m_current_filter = nullptr;
  Filter* m_current_joint_filter = nullptr;
  pjs::Context::Location m_current_location;
  bool m_has_on_start = false;
  bool m_has_on_end = false;

  friend class pjs::ObjectTemplate<PipelineDesigner>;
};

//
// PipelineProducer
//

class PipelineProducer : public pjs::ObjectTemplate<PipelineProducer> {
public:

  //
  // PipelineProducer::Constructor
  //

  class Constructor : public pjs::FunctionTemplate<Constructor> {
  public:
    void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret);
  };

  auto start(int argc = 0, pjs::Value *argv = nullptr) -> Pipeline*;

private:
  PipelineProducer(PipelineLayout *layout)
    : m_layout(layout) {}

  pjs::Ref<PipelineLayout> m_layout;

  friend class pjs::ObjectTemplate<PipelineProducer>;
};

} // namespace pipy

#endif // API_PIPELINE_HPP
