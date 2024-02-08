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
  m_instance->m_modules[m_id] = nullptr;
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

void Module::load(const std::string &name, const std::string &source) {
  m_source.filename = name;
  m_source.content = source;
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

void Module::execute(Context &ctx, int l, Tree::Imports *imports, Value &result) {
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

} // namespace pjs
