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

#include "stmt.hpp"
#include "module.hpp"

#include <algorithm>

namespace pjs {

//
// Stmt
//

Stmt::Stmt()
{
}

Stmt::~Stmt()
{
}

bool Stmt::execute(Context &ctx, Value &result) {
  Result res;
  execute(ctx, res);
  result = res.value;
  return !res.is_throw();
}

namespace stmt {

thread_local static ConstStr s_default("default");

//
// Block
//

bool Block::is_expression() const {
  return m_stmts.size() == 1 && m_stmts.front()->is_expression();
}

bool Block::declare(Module *module, Tree::Scope &scope, Error &error) {
  Tree::Scope s(Tree::Scope::BLOCK, &scope);
  for (const auto &p : m_stmts) {
    if (!p->declare(module, s, error)) {
      return false;
    }
  }
  return true;
}

void Block::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  for (const auto &p : m_stmts) {
    p->resolve(module, ctx, l, imports);
  }
}

void Block::execute(Context &ctx, Result &result) {
  if (!m_stmts.empty()) {
    for (const auto &p : m_stmts) {
      p->execute(ctx, result);
      if (!result.is_done()) return;
    }
    result.set_done();
  }
}

void Block::dump(std::ostream &out, const std::string &indent) {
  out << indent << "block" << std::endl;
  auto indent_str = indent + "  ";
  for (const auto &p : m_stmts) {
    p->dump(out, indent_str);
  }
}

//
// Label
//

bool Label::declare(Module *module, Tree::Scope &scope, Error &error) {
  Tree::Scope s(m_name, &scope);
  return m_stmt->declare(module, s, error);
}

void Label::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  m_stmt->resolve(module, ctx, l, imports);
}

void Label::execute(Context &ctx, Result &result) {
  m_stmt->execute(ctx, result);
  if (result.is_break() && result.label == m_name) {
    result.set_done();
  }
}

void Label::dump(std::ostream &out, const std::string &indent) {
  out << indent << "label " << m_name->str() << std::endl;
  m_stmt->dump(out, indent + "  ");
}

//
// Evaluate
//

bool Evaluate::declare(Module *module, Tree::Scope &scope, Error &error) {
  return m_expr->declare(module, scope, error);
}

void Evaluate::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  m_expr->resolve(module, ctx, l, imports);
}

void Evaluate::execute(Context &ctx, Result &result) {
  if (m_expr->eval(ctx, result.value)) {
    result.set_done();
    if (m_export) {
      auto obj = m_module->exports_object();
      obj->type()->set(obj, m_export->id, result.value);
    }
  } else {
    result.value = ctx.error().value;
    result.set_throw();
  }
}

void Evaluate::dump(std::ostream &out, const std::string &indent) {
  out << indent << "eval" << std::endl;
  m_expr->dump(out, indent + "  ");
}

bool Evaluate::declare_export(Module *module, bool is_default, Error &error) {
  m_module = module;
  m_export = module->add_export(s_default, Str::empty);
  return m_expr->declare(module, module->scope(), error);
}

//
// Var
//

bool Var::declare(Module *module, Tree::Scope &scope, Error &error) {
  auto name = m_identifier->name();
  auto s = scope.parent();
  while (!s->is_root()) s = s->parent();
  if (is_fiber(name->str())) {
    if (!check_reserved(name->str(), error)) return false;
    s->declare_fiber_var(name, module);
  } else {
    s->declare_var(name);
  }
  if (m_expr && !m_expr->declare(module, scope, error)) return false;
  return true;
}

void Var::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  if (m_expr) {
    m_identifier->resolve(module, ctx, l, imports);
    m_expr->resolve(module, ctx, l, imports);
  }
}

void Var::execute(Context &ctx, Result &result) {
  if (m_expr) {
    Value val;
    if (m_expr->eval(ctx, val) && m_identifier->assign(ctx, val)) {
      result.set_done();
    } else {
      result.value = ctx.error().value;
      result.set_throw();
    }
  }
}

bool Var::declare_export(Module *module, bool is_default, Error &error) {
  auto name = m_identifier->name();
  if (!check_reserved(name->str(), error)) return false;
  if (is_fiber(name->str())) {
    error.tree = this;
    error.message = "cannot export a fiber variable";
    return false;
  }
  if (is_default) {
    module->add_export(s_default, name);
  } else {
    module->add_export(name, name);
  }
  if (m_expr && !m_expr->declare(module, module->scope(), error)) return false;
  return true;
}

void Var::dump(std::ostream &out, const std::string &indent) {
  out << indent << "var " << m_identifier->name()->str() << std::endl;
  if (m_expr) m_expr->dump(out, indent + "  ");
}

bool Var::check_reserved(const std::string &name, Error &error) {
  if (!is_reserved(name)) return true;
  error.tree = this;
  error.message = "reserved variable name '" + name + "'";
  return false;
}

bool Var::is_reserved(const std::string &name) {
  for (auto c : name) {
    if (c != '$') return false;
  }
  return true;
}

bool Var::is_fiber(const std::string &name) {
  return name[0] == '$';
}

//
// Function
//

bool Function::declare(Module *module, Tree::Scope &scope, Error &error) {
  m_is_definition = scope.parent()->is_root();
  auto name = m_identifier->name();
  auto s = scope.parent();
  while (!s->is_root()) s = s->parent();
  if (name->str()[0] == '$') {
    error.tree = this;
    error.message = "reserved function name '" + name->str() + "'";
    return false;
  } else {
    s->declare_var(name, m_is_definition ? m_expr.get() : nullptr);
    return m_expr->declare(module, scope, error);
  }
}

void Function::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  m_identifier->resolve(module, ctx, l, imports);
  m_expr->resolve(module, ctx, l, imports);
}

void Function::execute(Context &ctx, Result &result) {
  if (!m_is_definition) {
    Value val;
    if (m_expr->eval(ctx, val) && m_identifier->assign(ctx, val)) {
      result.set_done();
    } else {
      result.value = ctx.error().value;
      result.set_throw();
    }
  }
}

bool Function::declare_export(Module *module, bool is_default, Error &error) {
  auto name = m_identifier->name();
  if (name->str()[0] == '$') {
    error.tree = this;
    error.message = "reserved function name '" + name->str() + "'";
    return false;
  }
  if (is_default) {
    module->add_export(s_default, name, m_expr.get());
  } else {
    module->add_export(name, name, m_expr.get());
  }
  m_is_definition = true;
  return m_expr->declare(module, module->scope(), error);
}

void Function::dump(std::ostream &out, const std::string &indent) {
  out << indent << "function " << m_identifier->name()->str() << std::endl;
  m_expr->dump(out, indent + "  ");
}

//
// If
//

bool If::declare(Module *module, Tree::Scope &scope, Error &error) {
  if (!m_cond->declare(module, scope, error)) return false;
  if (!m_then->declare(module, scope, error)) return false;
  if (m_else && !m_else->declare(module, scope, error)) return false;
  return true;
}

void If::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  m_cond->resolve(module, ctx, l, imports);
  m_then->resolve(module, ctx, l, imports);
  if (m_else) m_else->resolve(module, ctx, l, imports);
}

void If::execute(Context &ctx, Result &result) {
  Value val;
  if (!m_cond->eval(ctx, val)) {
    result.value = ctx.error().value;
    result.set_throw();
    return;
  }

  if (val.to_boolean()) {
    m_then->execute(ctx, result);
  } else if (m_else) {
    m_else->execute(ctx, result);
  }
}

void If::dump(std::ostream &out, const std::string &indent) {
  out << indent << "if" << std::endl;
  auto indent_str = indent + "  ";
  m_cond->dump(out, indent_str);
  out << indent << "then" << std::endl;
  m_then->dump(out, indent_str);
  if (m_else) {
    out << indent << "else" << std::endl;
    m_else->dump(out, indent_str);
  }
}

//
// Switch
//

bool Switch::declare(Module *module, Tree::Scope &scope, Error &error) {
  Tree::Scope s(Tree::Scope::SWITCH, &scope);
  if (!m_cond->declare(module, s, error)) return false;
  for (const auto &p : m_cases) {
    if (p.first && !p.first->declare(module, s, error)) return false;
    if (p.second && !p.second->declare(module, s, error)) return false;
  }
  return true;
}

void Switch::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  m_cond->resolve(module, ctx, l, imports);
  for (const auto &p : m_cases) {
    if (p.first) p.first->resolve(module, ctx, l, imports);
    if (p.second) p.second->resolve(module, ctx, l, imports);
  }
}

void Switch::execute(Context &ctx, Result &result) {
  Value cond_val;
  if (!m_cond->eval(ctx, cond_val)) {
    result.value = ctx.error().value;
    result.set_throw();
    return;
  }

  auto def = m_cases.end();
  auto p = m_cases.begin();
  while (p != m_cases.end()) {
    if (auto e = p->first.get()) {
      Value val;
      if (!e->eval(ctx, val)) {
        result.value = ctx.error().value;
        result.set_throw();
        return;
      }
      if (Value::is_equal(cond_val, val)) break;
    } else {
      def = p;
    }
    p++;
  }

  if (p == m_cases.end()) p = def;

  while (p != m_cases.end()) {
    if (auto s = p->second.get()) {
      s->execute(ctx, result);
      if (result.is_break()) break;
      if (!result.is_done()) return;
    }
    p++;
  }

  result.set_done();
}

void Switch::dump(std::ostream &out, const std::string &indent) {
  out << indent << "switch" << std::endl;
  auto indent_str = indent + "  ";
  m_cond->dump(out, indent_str);
  for (const auto &p : m_cases) {
    if (p.first) {
      out << indent << "case" << std::endl;
      p.first->dump(out, indent_str);
    } else {
      out << indent << "default" << std::endl;
    }
    if (p.second) {
      out << indent << "then" << std::endl;
      p.second->dump(out, indent_str);
    }
  }
}

//
// Break
//

bool Break::declare(Module *module, Tree::Scope &scope, Error &error) {
  auto *s = &scope;
  if (m_label) {
    while (s && s->label() != m_label->name()) {
      s = s->parent();
    }
  } else {
    while (s && s->kind() != Tree::Scope::SWITCH) {
      s = s->parent();
    }
  }
  if (!s) {
    error.tree = this;
    error.message = "illegal break";
    return false;
  }
  return true;
}

void Break::execute(Context &ctx, Result &result) {
  if (m_label) {
    result.set_break(m_label->name());
  } else {
    result.set_break();
  }
}

void Break::dump(std::ostream &out, const std::string &indent) {
  out << indent << "break" << std::endl;
}

//
// Return
//

bool Return::declare(Module *module, Tree::Scope &scope, Error &error) {
  if (m_expr && !m_expr->declare(module, scope, error)) return false;
  return true;
}

void Return::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  if (m_expr) m_expr->resolve(module, ctx, l, imports);
}

void Return::execute(Context &ctx, Result &result) {
  if (m_expr) {
    if (m_expr->eval(ctx, result.value)) {
      result.set_return();
    } else {
      result.value = ctx.error().value;
      result.set_throw();
    }
  } else {
    result.value = Value::undefined;
    result.set_return();
  }
}

void Return::dump(std::ostream &out, const std::string &indent) {
  out << indent << "return" << std::endl;
  if (m_expr) m_expr->dump(out, indent + "  ");
}

//
// Throw
//

bool Throw::declare(Module *module, Tree::Scope &scope, Error &error) {
  if (m_expr && !m_expr->declare(module, scope, error)) return false;
  return true;
}

void Throw::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  if (m_expr) m_expr->resolve(module, ctx, l, imports);
}

void Throw::execute(Context &ctx, Result &result) {
  if (m_expr) {
    if (m_expr->eval(ctx, result.value)) {
      ctx.backtrace(source(), line(), column());
      result.set_throw();
    } else {
      result.value = ctx.error().value;
      result.set_throw();
    }
  } else {
    result.value = Value::undefined;
    result.set_throw();
  }
}

void Throw::dump(std::ostream &out, const std::string &indent) {
  out << indent << "throw" << std::endl;
  if (m_expr) m_expr->dump(out, indent + "  ");
}

//
// Try
//

Try::Try(Stmt *try_clause, Stmt *catch_clause, Stmt *finally_clause, Expr *exception_variable)
  : m_try(try_clause)
  , m_catch(catch_clause)
  , m_finally(finally_clause)
  , m_exception_variable(exception_variable)
  , m_catch_scope(Scope::CATCH)
{
  if (exception_variable) {
    m_catch_scope.declare_arg(exception_variable);
  }
}

bool Try::declare(Module *module, Tree::Scope &scope, Error &error) {
  if (!m_try->declare(module, scope, error)) return false;
  if (m_catch) {
    m_catch_scope.parent(&scope);
    if (!m_catch->declare(module, m_catch_scope, error)) return false;
  }
  if (m_finally && !m_finally->declare(module, scope, error)) return false;
  return true;
}

void Try::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  m_try->resolve(module, ctx, l, imports);
  if (m_catch) {
    Context cctx(
      ctx, 0, nullptr,
      pjs::Scope::make(
        ctx.instance(),
        ctx.scope(),
        m_catch_scope.size(),
        m_catch_scope.variables()
      )
    );
    m_catch->resolve(module, cctx, l, imports);
  }
  if (m_finally) m_finally->resolve(module, ctx, l, imports);
}

void Try::execute(Context &ctx, Result &result) {
  m_try->execute(ctx, result);
  if (result.is_throw() && m_catch) {
    if (result.value.is<pjs::Error>()) {
      result.value.as<pjs::Error>()->backtrace(ctx.error().backtrace);
    }
    ctx.reset();
    Context cctx(ctx, 1, &result.value, ctx.scope());
    if (auto scope = m_catch_scope.instantiate(cctx)) {
      m_catch->execute(cctx, result);
      scope->clear();
    } else {
      result.value = ctx.error().value;
      result.set_throw();
    }
  }
  if (m_finally) {
    if (result.is_throw()) {
      Result res;
      m_finally->execute(ctx, res);
      if (res.is_throw()) result.value = res.value;
    } else {
      m_finally->execute(ctx, result);
    }
  }
}

void Try::dump(std::ostream &out, const std::string &indent) {
  out << indent << "try" << std::endl;
  auto indent_str = indent + "  ";
  m_try->dump(out, indent_str);
  if (m_catch) {
    out << indent << "catch" << std::endl;
    m_catch->dump(out, indent_str);
  }
  if (m_finally) {
    out << indent << "finally" << std::endl;
    m_finally->dump(out, indent_str);
  }
}

//
// Import
//

bool Import::declare(Module *module, Tree::Scope &scope, Error &error) {
  thread_local static ConstStr s_star("*");
  if (scope.parent()->kind() != Tree::Scope::MODULE) {
    error.tree = this;
    error.message = "illegal import";
    return false;
  }
  if (m_list.empty()) {
    module->add_import(nullptr, nullptr, Str::make(m_from));
    return true;
  } else {
    Ref<Str> from(Str::make(m_from));
    for (const auto &p : m_list) {
      Ref<Str> id(Str::make(p.first));
      Ref<Str> as(Str::make(p.second));
      module->add_import(
        as == Str::empty ? id : as,
        id == s_star ? nullptr : id.get(),
        from
      );
    }
    return true;
  }
}

void Import::dump(std::ostream &out, const std::string &indent) {
  auto indent_str = indent + "  ";
  out << indent << "import from '" << m_from << "'" << std::endl;
  for (const auto &p : m_list) {
    out << indent_str << "'" << p.first << "'";
    if (!p.second.empty()) out << " as " << p.second;
    out << std::endl;
  }
}

//
// Export
//

bool Export::declare(Module *module, Tree::Scope &scope, Error &error) {
  if (scope.parent()->kind() != Tree::Scope::MODULE) {
    error.tree = this;
    error.message = "illegal export";
    return false;
  }
  if (m_stmt) {
    if (auto exportable = dynamic_cast<Exportable*>(m_stmt.get())) {
      return exportable->declare_export(module, m_default, error);
    } else {
      error.tree = this;
      error.message = "cannot export";
      return false;
    }
  } else if (m_from.empty()) {
    for (const auto &p : m_list) {
      Ref<Str> id(Str::make(p.first));
      Ref<Str> as(Str::make(p.second));
      module->add_export(as == Str::empty ? id : as, id);
    }
    return true;
  } else {
    Ref<Str> from(Str::make(m_from));
    for (const auto &p : m_list) {
      Ref<Str> id(Str::make(p.first));
      Ref<Str> as(Str::make(p.second));
      module->add_export(
        as == Str::empty ? id : as,
        module->add_import(nullptr, id, from)
      );
    }
    return true;
  }
}

void Export::resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) {
  if (m_stmt) {
    m_stmt->resolve(module, ctx, l, imports);
  }
}

void Export::execute(Context &ctx, Result &result) {
  if (m_stmt) {
    m_stmt->execute(ctx, result);
  }
}

void Export::dump(std::ostream &out, const std::string &indent) {
  auto indent_str = indent + "  ";
  if (m_stmt) {
    out << indent << (m_default ? "export default" : "export") << std::endl;
    m_stmt->dump(out, indent_str);
  } else {
    out << indent << "export";
    if (!m_from.empty()) out << " from '" << m_from << "'";
    out << std::endl;
    for (const auto &p : m_list) {
      out << indent_str << p.first;
      if (!p.second.empty()) out << " as '" << p.second << "'";
      out << std::endl;
    }
  }
}

} // namespace stmt

} // namespace pjs
