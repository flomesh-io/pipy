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
    enum Type { DONE, RETURN, BREAK, CONTINUE };
    Type type = DONE;
    Str* label = nullptr;
    Value value;

    bool is_done() const { return type == DONE; }
    bool is_return() const { return type == RETURN; }
    bool is_break() const { return type == BREAK; }
    bool is_continue() const { return type == CONTINUE; }

    void set_done() { type = DONE; label = nullptr; }
    void set_return() { type = RETURN; label = nullptr; }
    void set_break(Str *l = nullptr) { type = BREAK; label = l; }
    void set_continue(Str *l = nullptr) { type = CONTINUE; label = l; }
  };

  //
  // Statement base methods
  //

  Stmt();
  virtual ~Stmt();
  virtual bool is_expression() const { return false; }
  virtual void execute(Context &ctx, Result &result) {};
  virtual void dump(std::ostream &out, const std::string &indent = "") = 0;

  //
  // Statement execution
  //

  void execute(Context &ctx, Value &result);
};

//
// Exportable
//

class Exportable : public Stmt {
public:
  virtual bool declare_export(Module *module, bool is_default, Error &error) = 0;
};

namespace stmt {

//
// Block
//

class Block : public Stmt {
public:
  Block() {}
  Block(std::list<std::unique_ptr<Stmt>> &&stmts) : m_stmts(std::move(stmts)) {}

  virtual bool is_expression() const override;
  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
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

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Ref<Str> m_name;
  std::unique_ptr<Stmt> m_stmt;
};

//
// Evaluate
//

class Evaluate : public Exportable {
public:
  Evaluate(Expr *expr) : m_expr(expr) {}

  virtual bool is_expression() const override { return true; }
  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual bool declare_export(Module *module, bool is_default, Error &error) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Module* m_module = nullptr;
  Export* m_export = nullptr;
  std::unique_ptr<Expr> m_expr;
};

//
// Var
//

class Var : public Exportable {
public:
  Var(expr::Identifier *name, Expr *expr)
    : m_identifier(name), m_expr(expr) {}

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual bool declare_export(Module *module, bool is_default, Error &error) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<expr::Identifier> m_identifier;
  std::unique_ptr<Expr> m_resolved;
  std::unique_ptr<Expr> m_expr;

  bool check_reserved(const std::string &name, Error &error);

  static bool is_fiber(const std::string &name);
  static bool is_reserved(const std::string &name);
};

//
// Function
//

class Function : public Exportable {
public:
  Function(expr::Identifier *name, Expr *expr)
    : m_identifier(name), m_expr(expr) {}

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual bool declare_export(Module *module, bool is_default, Error &error) override;
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

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
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

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
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
  Break(expr::Identifier *label) : m_label(label) {}

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<expr::Identifier> m_label;
};

//
// Return
//

class Return : public Stmt {
public:
  Return(Expr *expr) : m_expr(expr) {}

  auto value() const -> Expr* { return m_expr.get(); }

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
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

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
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
  Try(Stmt *try_clause, Stmt *catch_clause, Stmt *finally_clause, Expr *exception_variable);

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Stmt> m_try;
  std::unique_ptr<Stmt> m_catch;
  std::unique_ptr<Stmt> m_finally;
  std::unique_ptr<Expr> m_exception_variable;
  Scope m_catch_scope;
};

//
// Import
//

class Import : public Stmt {
public:
  Import(std::list<std::pair<std::string, std::string>> &&list, const std::string &from)
    : m_list(std::move(list)), m_from(from) {}

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::list<std::pair<std::string, std::string>> m_list;
  std::string m_from;
};

//
// Export
//

class Export : public Stmt {
public:
  Export(Stmt *stmt, bool is_default) : m_stmt(stmt), m_default(is_default) {}
  Export(std::list<std::pair<std::string, std::string>> &&list, const std::string &from)
    : m_list(std::move(list)), m_from(from), m_default(false) {}

  virtual bool declare(Module *module, Scope &scope, Error &error, bool is_lval) override;
  virtual void resolve(Module *module, Context &ctx, int l, Tree::LegacyImports *imports) override;
  virtual void execute(Context &ctx, Result &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::list<std::pair<std::string, std::string>> m_list;
  std::string m_from;
  std::unique_ptr<Stmt> m_stmt;
  bool m_default;
};

} // namespace stmt

//
// Statement constructors
//

inline Stmt* block() { return new stmt::Block(); }
inline Stmt* block(std::list<std::unique_ptr<Stmt>> &&stmts) { return new stmt::Block(std::move(stmts)); }
inline Stmt* label(const std::string &name, Stmt *stmt) { return new stmt::Label(name, stmt); }
inline Stmt* evaluate(Expr *expr) { return new stmt::Evaluate(expr); }
inline Stmt* var(expr::Identifier *name, Expr *expr = nullptr) { return new stmt::Var(name, expr); }
inline Stmt* function(expr::Identifier *name, Expr *expr) { return new stmt::Function(name, expr); }
inline Stmt* if_else(Expr *cond, Stmt *then_clause, Stmt *else_clause = nullptr) { return new stmt::If(cond, then_clause, else_clause); }
inline Stmt* switch_case(Expr *cond, std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> &&cases) { return new stmt::Switch(cond, std::move(cases)); }
inline Stmt* try_catch(Stmt *try_clause, Stmt *catch_clause, Stmt *finally_clause, Expr *exception_variable) { return new stmt::Try(try_clause, catch_clause, finally_clause, exception_variable); }
inline Stmt* flow_break() { return new stmt::Break(); }
inline Stmt* flow_break(expr::Identifier *label) { return new stmt::Break(label); }
inline Stmt* flow_return(Expr *expr = nullptr) { return new stmt::Return(expr); }
inline Stmt* flow_throw(Expr *expr = nullptr) { return new stmt::Throw(expr); }
inline Stmt* module_import(std::list<std::pair<std::string, std::string>> &&list, const std::string &from) { return new stmt::Import(std::move(list), from); }
inline Stmt* module_export(std::list<std::pair<std::string, std::string>> &&list, const std::string &from = std::string()) { return new stmt::Export(std::move(list), from); }
inline Stmt* module_export(Stmt *stmt) { return new stmt::Export(stmt, false); }
inline Stmt* module_export_default(Stmt *stmt) { return new stmt::Export(stmt, true); }

} // namespace pjs

#endif // PJS_STMT_HPP
