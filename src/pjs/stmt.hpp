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

#ifndef PJS_STMT_HPP
#define PJS_STMT_HPP

#include "expr.hpp"

#include <list>
#include <string>

namespace pjs {

//
// Statement base
//

class Stmt {
public:

  //
  // Statement execution result
  //

  struct Result {
    enum Type { DONE, RETURN, BREAK, CONTINUE, THROW };
    Type type = DONE;
    Str* label = nullptr;
    Value value;

    bool is_done() const { return type == DONE; }
    bool is_return() const { return type == RETURN; }
    bool is_break() const { return type == BREAK; }
    bool is_continue() const { return type == CONTINUE; }
    bool is_throw() const { return type == THROW; }

    void set_done() { type = DONE; label = nullptr; }
    void set_return() { type = RETURN; label = nullptr; }
    void set_break(Str *l = nullptr) { type = BREAK; label = l; }
    void set_continue(Str *l = nullptr) { type = CONTINUE; label = l; }
    void set_throw() { type = THROW; label = nullptr; }
  };

  //
  // Statement base methods
  //

  Stmt();
  virtual ~Stmt();
  virtual void declare(Expr::Scope &scope) {}
  virtual void resolve(Context &ctx, int l = -1, Expr::Imports *imports = nullptr) {}
  virtual void execute(Context &ctx, Result &result) = 0;
  virtual void dump(std::ostream &out, const std::string &indent = "") = 0;
};

namespace stmt {

//
// Block
//

class Block : public Stmt {
public:
  Block(std::list<std::unique_ptr<Stmt>> &&stmts) : m_stmts(std::move(stmts)) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::list<std::unique_ptr<Stmt>> m_stmts;
};

//
// Evaluate
//

class Evaluate : public Stmt {
public:
  Evaluate(Expr *expr) : m_expr(expr) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_expr;
};

//
// Var
//

class Var : public Stmt {
public:
  Var(const std::string &name, Expr *expr)
    : m_identifier(new expr::Identifier(name)), m_expr(expr) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<expr::Identifier> m_identifier;
  std::unique_ptr<Expr> m_resolved;
  std::unique_ptr<Expr> m_expr;
};

//
// Function
//

class Function : public Stmt {
public:
  Function(const std::string &name, Expr *expr)
    : m_identifier(new expr::Identifier(name)), m_expr(expr) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<expr::Identifier> m_identifier;
  std::unique_ptr<Expr> m_expr;
  bool m_is_definition = false;
};

//
// If
//

class If : public Stmt {
public:
  If(Expr *cond, Stmt *then_clause, Stmt *else_clause)
    : m_cond(cond), m_then(then_clause), m_else(else_clause) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_cond;
  std::unique_ptr<Stmt> m_then;
  std::unique_ptr<Stmt> m_else;
};

//
// Switch
//

class Switch : public Stmt {
public:
  Switch(Expr *cond, std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> &&cases)
    : m_cond(cond), m_cases(std::move(cases)) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_cond;
  std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> m_cases;
};

//
// Break
//

class Break : public Stmt {
public:
  Break() {}

  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;
};

//
// Return
//

class Return : public Stmt {
public:
  Return(Expr *expr) : m_expr(expr) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_expr;
};

//
// Throw
//

class Throw : public Stmt {
public:
  Throw(Expr *expr) : m_expr(expr) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_expr;
};

//
// Try
//

class Try : public Stmt {
public:
  Try(Stmt *try_clause, Expr *catch_clause, Stmt *finally_clause)
    : m_try(try_clause), m_catch(catch_clause), m_finally(finally_clause) {}

  virtual void declare(Expr::Scope &scope) override;
  virtual void resolve(Context &ctx, int l, Expr::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Stmt> m_try;
  std::unique_ptr<Expr> m_catch;
  std::unique_ptr<Stmt> m_finally;
};

} // namespace stmt

} // namespace pjs

#endif // PJS_STMT_HPP
