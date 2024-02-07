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

class Stmt : public Tree {
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
  virtual void execute(Context &ctx, Result &result) = 0;
  virtual void dump(std::ostream &out, const std::string &indent = "") = 0;

  //
  // Statement execution
  //

  bool execute(Context &ctx, Value &result);
};

namespace stmt {

//
// Block
//

class Block : public Stmt {
public:
  Block() {}
  Block(std::list<std::unique_ptr<Stmt>> &&stmts) : m_stmts(std::move(stmts)) {}

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::list<std::unique_ptr<Stmt>> m_stmts;
};

//
// Label
//

class Label : public Stmt {
public:
  Label(const std::string &name, Stmt *stmt) : m_name(Str::make(name)), m_stmt(stmt) {}

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Ref<Str> m_name;
  std::unique_ptr<Stmt> m_stmt;
};

//
// Evaluate
//

class Evaluate : public Stmt {
public:
  Evaluate(Expr *expr) : m_expr(expr) {}

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
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

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
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

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
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

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
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
  Switch(Expr *cond, std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> &&cases, Stmt *default_case)
    : m_cond(cond), m_cases(std::move(cases)), m_default(default_case) {}

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_cond;
  std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> m_cases;
  std::unique_ptr<Stmt> m_default;
};

//
// Break
//

class Break : public Stmt {
public:
  Break() {}
  Break(const std::string &label) : m_label(Str::make(label)) {}

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Ref<Str> m_label;
};

//
// Return
//

class Return : public Stmt {
public:
  Return(Expr *expr) : m_expr(expr) {}

  auto value() const -> Expr* { return m_expr.get(); }

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
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

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
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

  virtual bool declare(Module *module, Tree::Scope &scope, Error &error) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::Imports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Stmt> m_try;
  std::unique_ptr<Expr> m_catch;
  std::unique_ptr<Stmt> m_finally;
};

} // namespace stmt

//
// Statement constructors
//

inline Stmt* block() { return new stmt::Block(); }
inline Stmt* block(std::list<std::unique_ptr<Stmt>> &&stmts) { return new stmt::Block(std::move(stmts)); }
inline Stmt* label(const std::string &name, Stmt *stmt) { return new stmt::Label(name, stmt); }
inline Stmt* evaluate(Expr *expr) { return new stmt::Evaluate(expr); }
inline Stmt* var(const std::string &name, Expr *expr = nullptr) { return new stmt::Var(name, expr); }
inline Stmt* function(const std::string &name, Expr *expr) { return new stmt::Function(name, expr); }
inline Stmt* if_else(Expr *cond, Stmt *then_clause, Stmt *else_clause = nullptr) { return new stmt::If(cond, then_clause, else_clause); }
inline Stmt* switch_case(Expr *cond, std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> &&cases, Stmt *default_case = nullptr) { return new stmt::Switch(cond, std::move(cases), default_case); }
inline Stmt* try_catch(Stmt *try_clause, Expr *catch_clause, Stmt *finally_clause) { return new stmt::Try(try_clause, catch_clause, finally_clause); }
inline Stmt* flow_break() { return new stmt::Break(); }
inline Stmt* flow_break(const std::string &label) { return new stmt::Break(label); }
inline Stmt* flow_return(Expr *expr = nullptr) { return new stmt::Return(expr); }
inline Stmt* flow_throw(Expr *expr = nullptr) { return new stmt::Throw(expr); }

} // namespace pjs

#endif // PJS_STMT_HPP
