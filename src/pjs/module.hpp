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

#ifndef PJS_MODULE_HPP
#define PJS_MODULE_HPP

#include "stmt.hpp"

namespace pjs {

//
// Module
//

class Module {
public:
  Module(Instance *instance);
  ~Module();

  auto instance() const -> Instance* { return m_instance; }
  auto id() const -> int { return m_id; }
  auto name() const -> const std::string& { return m_source.filename; }
  auto source() const -> const Source& { return m_source; }
  auto tree() const -> Stmt* { return m_tree.get(); }
  auto exports_object() const -> Object* { return m_exports_object; }

  void load(const std::string &name, const std::string &source);
  auto add_import(Str *name, Str *src_name, Str *path) -> Tree::Import*;
  auto add_export(Str *name, Str *src_name) -> Tree::Export*;
  void add_export(Str *name, Tree::Import *import);
  auto add_fiber_variable() -> int;
  auto new_fiber_data() -> Data*;
  auto find_import(Str *name) -> Tree::Import*;
  auto find_export(Str *name) -> int;
  bool compile(std::string &error, int &error_line, int &error_column);
  void resolve(const std::function<Module*(Module*, Str*)> &resolver);
  void execute(Context &ctx, int l, Tree::LegacyImports *imports, Value &result);

private:
  Instance* m_instance;
  int m_id;
  int m_fiber_variable_count = 0;
  Source m_source;
  Tree::Scope m_scope;
  std::unique_ptr<Stmt> m_tree;
  std::list<Tree::Import> m_imports;
  std::list<Tree::Export> m_exports;
  Ref<Class> m_exports_class;
  Ref<Object> m_exports_object;

  static void check_cyclic_import(Tree::Import *root, Tree::Import *current);

  friend class Instance;
};

} // namespace pjs

#endif // PJS_MODULE_HPP
