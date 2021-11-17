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

#ifndef USE_HPP
#define USE_HPP

#include "filter.hpp"

#include <list>

namespace pipy {

class Module;

//
// Use
//

class Use : public Filter {
public:
  Use(
    Module *module,
    pjs::Str *pipeline_name
  );

  Use(
    const std::list<Module*> &modules,
    pjs::Str *pipeline_name,
    pjs::Function *turn_down = nullptr
  );

  Use(
    const std::list<Module*> &modules,
    pjs::Str *pipeline_name,
    pjs::Str *pipeline_name_down,
    pjs::Function *turn_down = nullptr
  );

private:
  Use(const Use &r);
  ~Use();

  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  class Stage : public EventFunction {
  public:
    Stage(const Stage &r)
      : m_pipeline_def(r.m_pipeline_def)
      , m_pipeline_def_down(r.m_pipeline_def_down) {}

    Stage(PipelineDef *pipeline_def, PipelineDef *pipeline_def_down)
      : m_pipeline_def(pipeline_def)
      , m_pipeline_def_down(pipeline_def_down) {}

    void reset();

  private:
    virtual void on_event(Event *evt) override;

    auto input_down() -> EventTarget::Input*;

    Use* m_filter;
    Stage* m_prev;
    Stage* m_next;
    pjs::Ref<PipelineDef> m_pipeline_def;
    pjs::Ref<PipelineDef> m_pipeline_def_down;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<Pipeline> m_pipeline_down;
    bool m_chained = false;
    bool m_turned_down = false;

    friend class Use;
  };

  std::list<Module*> m_modules;
  std::list<Stage> m_stages;
  pjs::Ref<pjs::Str> m_pipeline_name;
  pjs::Ref<pjs::Str> m_pipeline_name_down;
  pjs::Ref<pjs::Function> m_turn_down;
  bool m_multiple = false;

  friend class Stage;
};

} // namespace pipy

#endif // USE_HPP
