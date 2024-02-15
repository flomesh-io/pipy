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

#include "module.hpp"
#include "parser.hpp"

namespace pjs {

Module::Module(Instance *instance)
  : m_instance(instance)
  , m_id(instance->m_modules.size())
  , m_scope(pjs::Tree::Scope::MODULE)
{
  instance->m_modules.push_back(this);
}

Module::~Module() {
  if (auto instance = m_instance) {
    instance->m_modules[m_id] = nullptr;
  }
}

void Module::load(const std::string &name, const std::string &source) {
  m_source.filename = name;
  m_source.content = source;
}

auto Module::add_import(Str *name, Str *src_name, Str *path) -> Tree::Import* {
  m_imports.emplace_back();
  auto &i = m_imports.back();
  i.alias = name ? name : src_name;
  i.name = src_name;
  i.path = path;
  return &i;
}

void Module::add_export(Str *name, Str *src_name) {
  m_exports.emplace_back();
  auto &e = m_exports.back();
  e.alias = name ? name : src_name;
  e.name = src_name;
}

void Module::add_export(Str *name, Tree::Import *import) {
  m_exports.emplace_back();
  auto &e = m_exports.back();
  e.alias = name;
  e.import = import;
}

auto Module::add_fiber_variable() -> int {
  return m_fiber_variable_count++;
}

auto Module::new_fiber_data() -> Data* {
  if (auto n = m_fiber_variable_count) {
    return Data::make(n);
  } else {
    return nullptr;
  }
}

auto Module::find_import(Str *name) -> Tree::Import* {
  for (auto &i : m_imports) {
    if (i.alias == name) {
      return &i;
    }
  }
  return nullptr;
}

auto Module::find_export(Str *name) -> int {
  for (auto &e : m_exports) {
    if (e.name == name) {
      return e.id;
    }
  }
  return -1;
}

bool Module::compile(std::string &error, int &error_line, int &error_column) {
  auto stmt = Parser::parse(&m_source, error, error_line, error_column);
  if (!stmt) return false;

  Tree::Error tree_error;
  if (!stmt->declare(this, m_scope, tree_error)) {
    auto tree = tree_error.tree;
    error = tree_error.message;
    error_line = tree->line();
    error_column = tree->column();
    return false;
  }

  m_tree = std::unique_ptr<Stmt>(stmt);
  return true;
}

void Module::resolve(const std::function<Module*(Str *path)> &resolver) {
  for (auto &imp : m_imports) {
    auto mod = resolver(imp.path);
    imp.module = mod;
    imp.exports = mod->exports_object();
    check_cyclic_import(&imp, &imp);
  }
}

void Module::execute(Context &ctx, int l, Tree::LegacyImports *imports, Value &result) {
  m_tree->resolve(this, ctx, l, imports);
  m_scope.instantiate(ctx);

  Stmt::Result res;
  m_tree->execute(ctx, res);
  if (res.is_throw()) {
    ctx.backtrace("(root)");
    return;
  }

  result = res.value;
}

void Module::init_exports() {
  if (m_exports_object) return;
  std::list<Field*> fields;
  int id = 0;
  for (auto &e : m_exports) {
    if (auto imp = e.import) {
      fields.push_back(
        Accessor::make(
          e.alias->str(),
          [=](Object *, Value &ret) { imp->get(ret); }
        )
      );
    } else {
      fields.push_back(Variable::make(e.alias->str(), 0, id));
      e.id = id++;
    }
  }
  m_exports_class = Class::make("", nullptr, fields);
}

void Module::check_cyclic_import(Tree::Import *root, Tree::Import *current) {
  for (const auto &e : current->module->m_exports) {
    if (e.alias == current->name || !current->name) {
      if (e.import) {
        if (e.import == root) {
          throw std::runtime_error("cyclic import");
        } else {
          check_cyclic_import(root, e.import);
        }
      }
      if (current->name) break;
    }
  }
}

} // namespace pjs
