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

#ifndef MODULE_HPP
#define MODULE_HPP

#include "pjs/pjs.hpp"
#include "context.hpp"
#include "pipeline-lb.hpp"

#include <map>
#include <set>

namespace pipy {

class Context;
class Configuration;
class PipelineLayout;
class PipelineLoadBalancer;

//
// ModuleBase
//

class ModuleBase : public pjs::RefCount<ModuleBase> {
public:
  auto label() const -> const std::string { return m_label; }

  virtual auto new_context(Context *base = nullptr) -> Context* = 0;
  virtual auto get_pipeline(pjs::Str *name) -> PipelineLayout* { return nullptr; }

  void for_each_pipeline(const std::function<void(PipelineLayout*)> &cb);
  void shutdown();

protected:
  ModuleBase(const std::string &label = std::string())
    : m_label(label) {}

  virtual ~ModuleBase() {}

private:
  std::string m_label;
  std::list<pjs::Ref<PipelineLayout>> m_pipelines;

  friend class pjs::RefCount<ModuleBase>;
  friend class PipelineLayout;
};

//
// Module
//

class Module : public ModuleBase {
public:
  auto index() const -> int { return m_index; }
  auto filename() const -> pjs::Str* { return m_filename; }

protected:
  Module(int index) : m_index(index) {}

  int m_index;
  pjs::Ref<pjs::Str> m_filename;

private:
  virtual void bind_exports(Worker *worker) = 0;
  virtual void bind_imports(Worker *worker) = 0;
  virtual void make_pipelines() = 0;
  virtual void bind_pipelines() = 0;
  virtual auto new_context_data(pjs::Object *prototype) -> pjs::Object* = 0;
  virtual void unload() = 0;

  friend class Configuration;
  friend class Worker;
};

//
// JSModule
//

class JSModule : public Module {
public:
  auto worker() const -> Worker* { return m_worker; }
  auto entrance_pipeline() -> PipelineLayout* { return m_entrance_pipeline; }
  auto find_named_pipeline(pjs::Str *name) -> PipelineLayout*;
  auto find_indexed_pipeline(int index) -> PipelineLayout*;
  void setup_pipeline_lb(PipelineLoadBalancer *plb);
  auto alloc_pipeline_lb(pjs::Str *name, EventTarget::Input *output) -> PipelineLoadBalancer::AsyncWrapper*;

  virtual auto new_context(Context *base = nullptr) -> Context* override;
  virtual auto get_pipeline(pjs::Str *name) -> PipelineLayout* override { return find_named_pipeline(name); }

private:
  bool load(const std::string &path, pjs::Value &result);
  virtual void unload() override;

  virtual void bind_exports(Worker *worker) override;
  virtual void bind_imports(Worker *worker) override;
  virtual void make_pipelines() override;
  virtual void bind_pipelines() override;

  virtual auto new_context_data(pjs::Object *prototype) -> pjs::Object* override {
    auto obj = new ContextDataBase(m_filename);
    m_context_class->init(obj, prototype);
    return obj;
  }

private:
  JSModule(Worker *worker, int index);
  ~JSModule();

  pjs::Ref<Worker> m_worker;
  pjs::Source m_source;
  std::unique_ptr<pjs::Stmt> m_script;
  std::unique_ptr<pjs::Tree::Imports> m_imports;
  pjs::Ref<Configuration> m_configuration;
  pjs::Ref<pjs::Class> m_context_class;
  std::map<pjs::Ref<pjs::Str>, PipelineLayout*> m_named_pipelines;
  std::map<int, PipelineLayout*> m_indexed_pipelines;
  PipelineLayout *m_entrance_pipeline = nullptr;

  friend class pjs::RefCount<Module>;
  friend class Configuration;
  friend class Worker;
};

} // namespace pipy

#endif // MODULE_HPP
