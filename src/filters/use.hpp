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
class Session;

//
// Use
//

class Use : public Filter {
public:
  Use();
  Use(Module *module, pjs::Str *pipeline_name, pjs::Function *when = nullptr);
  Use(const std::list<Module*> &modules, pjs::Str *pipeline_name, pjs::Function *when = nullptr);

private:
  Use(const Use &r);
  ~Use();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  class Stage {
  public:
    Stage(const Stage &r)
      : m_pipeline(r.m_pipeline) {}

    Stage(Pipeline *pipeline)
      : m_pipeline(pipeline) {}

    void reset() { m_session = nullptr; m_bypass = false; }
    void use(Context *context, Event *inp);

  private:
    Use* m_use;
    Stage* m_next;
    Pipeline* m_pipeline;
    pjs::Ref<Session> m_session;
    bool m_bypass = false;

    friend class Use;
  };

  std::list<Module*> m_modules;
  std::list<Stage> m_stages;
  pjs::Ref<pjs::Str> m_pipeline_name;
  pjs::Ref<pjs::Function> m_when;
  bool m_multiple = false;
  bool m_session_end = false;

  friend class Stage;
};

} // namespace pipy

#endif // USE_HPP