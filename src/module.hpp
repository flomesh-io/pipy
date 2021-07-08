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
#include "worker.hpp"
#include "task.hpp"

namespace pipy {

class Configuration;
class Pipeline;

class Module {
public:
  static void set_script(const std::string &path, const std::string &script);
  static void reset_script(const std::string &path);

  bool load(const std::string &path);
  void unload();
  void start();

  auto new_context_data(pjs::Object *prototype = nullptr) -> pjs::Object* {
    auto obj = new ContextDataBase(m_filename);
    if (prototype) obj->argv(prototype->as<ContextDataBase>()->argv());
    m_context_class->init(obj, prototype);
    return obj;
  }

  auto worker() const -> Worker* { return m_worker; }
  auto index() const -> int { return m_index; }
  auto path() const -> const std::string& { return m_path; }
  auto source() const -> const std::string& { return m_source; }

  auto find_named_pipeline(pjs::Str *name) -> Pipeline* {
    auto i = m_named_pipelines.find(name);
    if (i == m_named_pipelines.end()) return nullptr;
    return i->second;
  }

private:
  Module(Worker *worker, int index);

  Worker* m_worker;
  int m_index;
  std::string m_path;
  std::string m_source;
  std::unique_ptr<pjs::Expr> m_script;
  pjs::Ref<pjs::Str> m_filename;
  pjs::Ref<Configuration> m_configuration;
  pjs::Ref<pjs::Class> m_context_class;
  std::unordered_map<pjs::Ref<pjs::Str>, pjs::Ref<Pipeline>> m_named_pipelines;

  static std::map<std::string, std::string> s_overriden_scripts;

  friend class Configuration;
  friend class Worker;
};

} // namespace pipy

#endif // MODULE_HPP
