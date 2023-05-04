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

#ifndef FILTER_HPP
#define FILTER_HPP

#include "event.hpp"
#include "list.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pipy {

class Context;
class ModuleBase;
class PipelineLayout;
class Pipeline;
class Output;

//
// Filter
//

class Filter :
  public EventFunction,
  public List<Filter>::Item
{
public:

  //
  // Filter::Dump
  //

  struct Dump {
    enum OutType {
      NO_OUTPUT,
      OUTPUT_FROM_SELF,
      OUTPUT_FROM_SUBS,
      OUTPUT_FROM_OTHERS,
    };

    enum SubType {
      NO_SUBS,
      BRANCH,
      DEMUX,
      MUX,
    };

    struct Sub {
      int index = -1;
      std::string name;
    };

    std::string name;
    std::vector<Sub> subs;
    SubType sub_type = NO_SUBS;
    OutType out_type = OUTPUT_FROM_SELF;
  };

  virtual ~Filter() {}

  auto module() const -> ModuleBase*;
  auto context() const -> Context*;

  void set_location(const pjs::Context::Location &loc) { m_location = loc; }
  void add_sub_pipeline(PipelineLayout *layout);
  void add_sub_pipeline(pjs::Str *name);
  void add_sub_pipeline(int index);
  auto num_sub_pipelines() const -> int { return m_subs->size(); }

  auto sub_pipeline(
    int i,
    bool clone_context,
    Input *chain_to = nullptr,
    Output *output_to = nullptr
  ) -> Pipeline*;

  auto sub_pipeline(
    PipelineLayout *layout,
    bool clone_context,
    Input *chain_to = nullptr,
    Output *output_to = nullptr
  ) -> Pipeline*;

  virtual void bind();
  virtual auto clone() -> Filter* = 0;
  virtual void chain();
  virtual void reset();
  virtual void process(Event *evt) = 0;
  virtual void shutdown();
  virtual void dump(Dump &d);

  auto pipeline() const -> Pipeline* { return m_pipeline; }
  void output(Event *evt);
  bool output(const pjs::Value &evt);
  bool callback(pjs::Function *func, int argc, pjs::Value argv[], pjs::Value &result);
  bool eval(pjs::Value &param, pjs::Value &result);
  bool eval(pjs::Function *func, pjs::Value &result);
  void error(StreamEnd *end);
  void error(StreamEnd::Error type);
  void error(pjs::Error *error);
  void error(const char *format, ...);
  auto error_location(char *buf, size_t len) -> size_t;

protected:
  Filter();
  Filter(const Filter &r);

  using EventFunction::output;

private:
  struct Sub {
    int index = -1;
    pjs::Ref<pjs::Str> name;
    pjs::Ref<PipelineLayout> layout;
  };

  std::shared_ptr<std::vector<Sub>> m_subs;

  PipelineLayout* m_pipeline_layout = nullptr;
  Pipeline* m_pipeline = nullptr;
  pjs::Context::Location m_location;

  virtual void on_event(Event *evt) override;

  friend class Pipeline;
  friend class PipelineLayout;
};

} // namespace pipy

#endif // FILTER_HPP
