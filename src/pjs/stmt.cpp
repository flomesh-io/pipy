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

namespace stmt {

//
// Block
//

void Block::declare(Expr::Scope &scope) {
  Expr::Scope s(&scope);
  for (const auto &p : m_stmts) {
    p->declare(s);
  }
}

void Block::resolve(Context &ctx, int l, Expr::Imports *imports) {
  for (const auto &p : m_stmts) {
    p->resolve(ctx, l, imports);
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
// Evaluate
//

void Evaluate::declare(Expr::Scope &scope) {
  m_expr->declare(scope);
}

void Evaluate::resolve(Context &ctx, int l, Expr::Imports *imports) {
  m_expr->resolve(ctx, l, imports);
}

void Evaluate::execute(Context &ctx, Result &result) {
  if (m_expr->eval(ctx, result.value)) {
    result.set_done();
  } else {
    result.value.set(Error::make(ctx.error()));
    result.set_throw();
  }
}

void Evaluate::dump(std::ostream &out, const std::string &indent) {
  out << indent << "eval" << std::endl;
  m_expr->dump(out, indent + "  ");
}

//
// Var
//

void Var::declare(Expr::Scope &scope) {
  auto s = scope.parent;
  while (!s->is_function()) s = s->parent;
  auto name = m_identifier->name();
  if (s->variables.count(name) == 0) s->variables[name] = nullptr;
  if (m_expr) m_expr->declare(scope);
}

void Var::resolve(Context &ctx, int l, Expr::Imports *imports) {
  if (m_expr) {
    m_identifier->resolve(ctx, l, imports);
    m_expr->resolve(ctx, l, imports);
  }
}

void Var::execute(Context &ctx, Result &result) {
  if (m_expr) {
    Value val;
    if (m_expr->eval(ctx, val)) {
      m_identifier->assign(ctx, val);
      result.set_done();
    } else {
      result.value.set(Error::make(ctx.error()));
      result.set_throw();
    }
  }
}

void Var::dump(std::ostream &out, const std::string &indent) {
  out << indent << "var " << m_identifier->name()->str() << std::endl;
  m_expr->dump(out, indent + "  ");
}

//
// Function
//

void Function::declare(Expr::Scope &scope) {
  auto s = scope.parent;
  while (!s->is_function()) s = s->parent;
  auto name = m_identifier->name();
  m_is_definition = s->is_function();
  s->variables[name] = (m_is_definition ? m_expr.get() : nullptr);
  m_expr->declare(scope);
}

void Function::resolve(Context &ctx, int l, Expr::Imports *imports) {
  m_identifier->resolve(ctx, l, imports);
  m_expr->resolve(ctx, l, imports);
}

void Function::execute(Context &ctx, Result &result) {
  if (!m_is_definition) {
    Value val;
    if (m_expr->eval(ctx, val)) {
      m_identifier->assign(ctx, val);
      result.set_done();
    } else {
      result.value.set(Error::make(ctx.error()));
      result.set_throw();
    }
  }
}

void Function::dump(std::ostream &out, const std::string &indent) {
  out << indent << "function " << m_identifier->name()->str() << std::endl;
  m_expr->dump(out, indent + "  ");
}

//
// If
//

void If::declare(Expr::Scope &scope) {
  m_cond->declare(scope);
  m_then->declare(scope);
  if (m_else) m_else->declare(scope);
}

void If::resolve(Context &ctx, int l, Expr::Imports *imports) {
  m_cond->resolve(ctx, l, imports);
  m_then->resolve(ctx, l, imports);
  if (m_else) m_else->resolve(ctx, l, imports);
}

void If::execute(Context &ctx, Result &result) {
  Value val;
  if (!m_cond->eval(ctx, val)) {
    result.value.set(Error::make(ctx.error()));
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

void Switch::declare(Expr::Scope &scope) {
  m_cond->declare(scope);
  for (const auto &p : m_cases) {
    p.first->declare(scope);
    if (p.second) p.second->declare(scope);
  }
}

void Switch::resolve(Context &ctx, int l, Expr::Imports *imports) {
  m_cond->resolve(ctx, l, imports);
  for (const auto &p : m_cases) {
    p.first->resolve(ctx, l, imports);
    if (p.second) p.second->resolve(ctx, l, imports);
  }
}

void Switch::execute(Context &ctx, Result &result) {
  Value cond_val;
  if (!m_cond->eval(ctx, cond_val)) {
    result.value.set(Error::make(ctx.error()));
    result.set_throw();
    return;
  }

  auto p = m_cases.begin();
  while (p != m_cases.end()) {
    auto e = p->first.get();
    Value val;
    if (!e->eval(ctx, val)) {
      result.value.set(Error::make(ctx.error()));
      result.set_throw();
      return;
    }
    if (Value::is_equal(cond_val, val)) break;
    p++;
  }

  while (p != m_cases.end()) {
    if (auto s = p->second.get()) {
      s->execute(ctx, result);
      if (!result.is_done()) return;
    }
    p++;
  }
}

void Switch::dump(std::ostream &out, const std::string &indent) {
  out << indent << "switch" << std::endl;
  auto indent_str = indent + "  ";
  m_cond->dump(out, indent_str);
  for (const auto &p : m_cases) {
    out << indent << "case" << std::endl;
    p.first->dump(out, indent_str);
    if (p.second) {
      out << indent << "then" << std::endl;
      p.second->dump(out, indent_str);
    }
  }
}

//
// Break
//

void Break::execute(Context &ctx, Result &result) {
  result.set_break();
}

void Break::dump(std::ostream &out, const std::string &indent) {
  out << indent << "break" << std::endl;
}

//
// Return
//

void Return::declare(Expr::Scope &scope) {
  if (m_expr) m_expr->declare(scope);
}

void Return::resolve(Context &ctx, int l, Expr::Imports *imports) {
  if (m_expr) m_expr->resolve(ctx, l, imports);
}

void Return::execute(Context &ctx, Result &result) {
  if (m_expr) {
    if (m_expr->eval(ctx, result.value)) {
      result.set_return();
    } else {
      result.value.set(Error::make(ctx.error()));
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

} // namespace stmt

} // namespace pjs
