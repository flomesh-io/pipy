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

#ifndef PJS_EXPR_HPP
#define PJS_EXPR_HPP

#include "types.hpp"
#include "builtin.hpp"

#include <cmath>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <iostream>

namespace pjs {

class Stmt;

//
// Expression base
//

class Expr {
public:

  //
  // Expr::Reducer
  //

  class Reducer {
  public:

    // Expr::Reducer::Value
    class Value { public: virtual ~Value() {} };

    // Primitives
    virtual Value* type(Value *x) { return dummy(x); }
    virtual Value* undefined() { return nullptr; }
    virtual Value* null() { return undefined(); }
    virtual Value* boolean(bool b) { return undefined(); }
    virtual Value* number(double n) { return undefined(); }
    virtual Value* string(const std::string &s) { return undefined(); }

    // Objects
    virtual Value* is(Value *obj, Value *ctor) { return dummy(obj, ctor); }
    virtual Value* object(Value **kv, size_t count) { return dummy(kv, count); }
    virtual Value* array(Value **v, size_t count) { return dummy(v, count); }
    virtual Value* function(int argc, Expr **inputs, Expr *output) { return undefined(); }

    // Property access
    virtual Value* get(Value *obj, Value *key) { return dummy(obj, key); }
    virtual Value* set(Value *obj, Value *key, Value *val) { return dummy(obj, key, val); }
    virtual Value* del(Value *obj, Value *key) { return dummy(obj, key); }
    virtual Value* has(Value *obj, Value *key) { return dummy(obj, key); }

    // Function invocation
    virtual Value* call(Value *fn, Value **argv, int argc) { return dummy(fn, argv, argc); }
    virtual Value* construct(Value *fn, Value **argv, int argc) { return dummy(fn, argv, argc); }

    // Variables
    virtual Value* get(const std::string &name) { return undefined(); }
    virtual Value* set(const std::string &name, Value *val) { return dummy(val); }

    // Numeric
    virtual Value* pos(Value *x) { return dummy(x); }
    virtual Value* neg(Value *x) { return dummy(x); }
    virtual Value* add(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* sub(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* mul(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* div(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* rem(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* pow(Value *a, Value *b) { return dummy(a, b); }

    // Bitwise
    virtual Value* shl(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* shr(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* usr(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* bit_not(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* bit_and(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* bit_or (Value *a, Value *b) { return dummy(a, b); }
    virtual Value* bit_xor(Value *a, Value *b) { return dummy(a, b); }

    // Logical
    virtual Value* bool_not(Value *x) { return dummy(x); }
    virtual Value* bool_and(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* bool_or(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* null_or(Value *a, Value *b) { return dummy(a, b); }

    // Comparison
    virtual Value* eql(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* neq(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* same(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* diff(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* gt(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* ge(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* lt(Value *a, Value *b) { return dummy(a, b); }
    virtual Value* le(Value *a, Value *b) { return dummy(a, b); }

    // Select
    virtual Value* select(Value *a, Value *b, Value *c) { return dummy(a, b, c); }

    // Compound
    virtual Value* compound(Value **v, size_t count) { return dummy(v, count); }

    // Free values
    virtual void free(Value *val) {}

  protected:
    void free(Value **argv, int argc) {
      for (int i = 0; i < argc; i++) {
        free(argv[i]);
      }
    }

    Value* dummy(Value *a) { free(a); return undefined(); }
    Value* dummy(Value *a, Value *b) { free(a); free(b); return undefined(); }
    Value* dummy(Value *a, Value *b, Value *c) { free(a); free(b); free(c); return undefined(); }
    Value* dummy(Value *a, Value **argv, int argc) { free(a); free(argv, argc); return undefined(); }
    Value* dummy(Value **argv, int argc) { free(argv, argc); return undefined(); }
  };

  //
  // Expr::Imports
  //

  class Imports {
  public:
    void add(Str *name, int file, Str *original_name);
    bool get(Str *name, int *file, Str **original_name);

  private:
    std::map<Ref<Str>, std::pair<int, Ref<Str>>> m_imports;
  };

  //
  // Expr::Scope
  //

  struct Scope {
    Scope* parent = nullptr;
    std::map<Str*, Expr*> variables;
    Scope(Scope *p = nullptr) : parent(p) {}
    bool is_function() const { return !parent; }
  };

  //
  // Expression base methods
  //

  Expr();
  virtual ~Expr();
  virtual bool is_left_value() const { return false; }
  virtual bool is_argument_list() const { return false; }
  virtual bool is_argument() const { return false; }
  virtual void to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {}
  virtual bool is_comma_ended() const { return false; }
  virtual void unpack(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {}
  virtual bool unpack(Context &ctx, Value &arg, int &var) { return true; }
  virtual bool eval(Context &ctx, Value &result) = 0;
  virtual bool assign(Context &ctx, Value &value) { return error(ctx, "cannot assign to a right-value"); }
  virtual bool clear(Context &ctx, Value &result) { return error(ctx, "cannot delete a value"); }
  virtual void declare(Expr::Scope &scope) {}
  virtual void resolve(Context &ctx, int l = -1, Imports *imports = nullptr) {}
  virtual auto reduce(Reducer &r) -> Reducer::Value* { return r.undefined(); }
  virtual auto reduce_lval(Reducer &r, Reducer::Value *rval) -> Reducer::Value* { return r.undefined(); }
  virtual void dump(std::ostream &out, const std::string &indent = "") = 0;

  //
  // Expression location in script
  //

  auto source() const -> const Source* { return m_source; }
  auto line() const -> int { return m_line; }
  auto column() const -> int { return m_column; }

  void locate(const Source *source, int line, int column) {
    m_source = source;
    m_line = line;
    m_column = column;
  }

protected:
  bool error(Context &ctx, const std::string &msg) {
    ctx.error(msg);
    ctx.backtrace(m_source, m_line, m_column);
    return false;
  }

private:
  const Source* m_source = nullptr;
  int m_line = 0;
  int m_column = 0;
};

namespace expr {

//
// Discard
//

class Discard : public Expr {
public:
  Discard(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// Compound
//

class Compound : public Expr {
public:
  Compound(Expr *list, Expr *append) {
    if (auto comp = dynamic_cast<Compound*>(list)) {
      comp->break_down(m_exprs);
      delete comp;
    } else {
      m_exprs.push_back(std::unique_ptr<Expr>(list));
    }
    if (append) {
      m_exprs.push_back(std::unique_ptr<Expr>(append));
    } else {
      m_is_comma_ended = true;
    }
  }

  void break_down(std::vector<std::unique_ptr<Expr>> &out) {
    out = std::move(m_exprs);
  }

  virtual bool is_argument_list() const override;
  virtual bool is_comma_ended() const override { return m_is_comma_ended; }
  virtual bool eval(Context &ctx, Value &result) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::vector<std::unique_ptr<Expr>> m_exprs;
  bool m_is_comma_ended = false;
};

//
// Concatenation
//

class Concatenation : public Expr {
public:
  Concatenation(std::list<std::unique_ptr<Expr>> &&exprs)
    : m_exprs(std::move(exprs)) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::list<std::unique_ptr<Expr>> m_exprs;
};

//
// Undefined
//

class Undefined : public Expr {
public:
  virtual bool eval(Context &ctx, Value &result) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;
};

//
// Null
//

class Null : public Expr {
public:
  virtual bool eval(Context &ctx, Value &result) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;
};

//
// BooleanLiteral
//

class BooleanLiteral : public Expr {
public:
  BooleanLiteral(bool b) : m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  bool m_b;
};

//
// NumberLiteral
//

class NumberLiteral : public Expr {
public:
  NumberLiteral(double n) : m_n(n) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  double m_n;
};

//
// StringLiteral
//

class StringLiteral : public Expr {
public:
  StringLiteral(const std::string &s) : m_s(Str::make(s)) {}

  auto s() const -> Str* { return m_s; }

  virtual bool eval(Context &ctx, Value &result) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Ref<Str> m_s;
};

//
// ObjectLiteral
//

class ObjectLiteral : public Expr {
public:
  ObjectLiteral(std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> &entries);

  virtual bool is_left_value() const override;
  virtual bool is_argument() const override;
  virtual void to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual void unpack(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual bool unpack(Context &ctx, Value &arg, int &var) override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  struct Entry {
    int index;
    std::unique_ptr<Expr> key;
    std::unique_ptr<Expr> value;
  };

  std::list<Entry> m_entries;
  Ref<Class> m_class;
};

//
// ArrayExpansion
//

class ArrayExpansion : public Expr {
public:
  ArrayExpansion(Expr *expr) : m_array(expr) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_array;
};

//
// ArrayLiteral
//

class ArrayLiteral : public Expr {
public:
  ArrayLiteral(std::list<std::unique_ptr<Expr>> &&list)
    : m_list(std::move(list)) {}

  virtual bool is_left_value() const override;
  virtual bool is_argument() const override;
  virtual void to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual void unpack(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual bool unpack(Context &ctx, Value &arg, int &var) override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::list<std::unique_ptr<Expr>> m_list;
};

//
// FunctionLiteral
//

class FunctionLiteral : public Expr {
public:
  FunctionLiteral(Expr *inputs, Expr *output);
  FunctionLiteral(Expr *inputs, Stmt *body);

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  struct Parameter {
    int index;
    Expr* value = nullptr;
    Expr* unpack = nullptr;
  };

  FunctionLiteral(Expr *inputs, Expr *output, Stmt *body);

  std::vector<std::unique_ptr<Expr>> m_inputs;
  std::unique_ptr<Expr> m_output;
  std::unique_ptr<Stmt> m_body;
  std::list<Parameter> m_parameters;
  size_t m_argc = 0;
  std::vector<pjs::Scope::Variable> m_variables;
  Ref<Method> m_method;
};

//
// Global
//

class Global : public Expr {
public:
  Global(const std::string &key) : m_key(Str::make(key)) {}
  Global(Str *key) : m_key(key) {}

  virtual bool is_left_value() const override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual bool assign(Context &ctx, Value &value) override;
  virtual bool clear(Context &ctx, Value &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Ref<Str> m_key;
  PropertyCache m_cache;
};

//
// Local
//

class Local : public Expr {
public:
  Local(const std::string &key) : m_key(Str::make(key)) {}
  Local(int l, Str *key) : m_l(l), m_key(key) {}

  virtual bool is_left_value() const override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual bool assign(Context &ctx, Value &value) override;
  virtual bool clear(Context &ctx, Value &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  int m_l = -1;
  Ref<Str> m_key;
  PropertyCache m_cache;
};

//
// Argument
//

class Argument : public Expr {
public:
  Argument(int i, int level) : m_i(i), m_level(level) {}

  virtual bool is_left_value() const override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual bool assign(Context &ctx, Value &value) override;
  virtual bool clear(Context &ctx, Value &result) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  int m_i;
  int m_level;
};

//
// Identifier
//

class Identifier : public Expr {
public:
  Identifier(const std::string &key) : m_key(Str::make(key)) {}

  auto name() const -> Str* { return m_key; }

  Expr* to_string() { return new StringLiteral(m_key->str()); }
  Expr* to_global() { return new Global(m_key->str()); }
  Expr* to_local() { return new Local(m_key->str()); }

  virtual bool is_left_value() const override;
  virtual bool is_argument() const override;
  virtual void to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual void unpack(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual bool unpack(Context &ctx, Value &arg, int &var) override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual bool assign(Context &ctx, Value &value) override;
  virtual bool clear(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  Ref<Str> m_key;
  int m_l = -1;
  Imports* m_imports = nullptr;
  std::unique_ptr<Expr> m_resolved;

  void resolve(Context &ctx);

  auto locate(Expr *expr) -> Expr* {
    expr->locate(source(), line(), column());
    return expr;
  }
};

//
// Property
//

class Property : public Expr {
public:
  Property(Expr *obj, Expr *key) : m_obj(obj), m_key(key) {}

  virtual bool is_left_value() const override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual bool assign(Context &ctx, Value &value) override;
  virtual bool clear(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_obj;
  std::unique_ptr<Expr> m_key;
  PropertyCache m_cache;
};

//
// OptionalProperty
//

class OptionalProperty : public Expr {
public:
  OptionalProperty(Expr *obj, Expr *key) : m_obj(obj), m_key(key) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_obj;
  std::unique_ptr<Expr> m_key;
  PropertyCache m_cache;
};

//
// Construction
//

class Construction : public Expr {
public:
  Construction(Expr *func) : m_func(func) {}
  Construction(Expr *func, std::vector<std::unique_ptr<Expr>> &&argv) : m_func(func), m_argv(std::move(argv)) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_func;
  std::vector<std::unique_ptr<Expr>> m_argv;
};

//
// Invocation
//

class Invocation : public Expr {
public:
  Invocation(Expr *func, std::vector<std::unique_ptr<Expr>> &&argv) : m_func(func), m_argv(std::move(argv)) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual auto reduce(Reducer &r) -> Reducer::Value* override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_func;
  std::vector<std::unique_ptr<Expr>> m_argv;
};

//
// OptionalInvocation
//

class OptionalInvocation : public Expr {
public:
  OptionalInvocation(Expr *func, std::vector<std::unique_ptr<Expr>> &&argv) : m_func(func), m_argv(std::move(argv)) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_func;
  std::vector<std::unique_ptr<Expr>> m_argv;
};

//
// Plus
//

class Plus : public Expr {
public:
  Plus(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// Negation
//

class Negation : public Expr {
public:
  Negation(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// Addition
//

class Addition : public Expr {
public:
  Addition(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Subtraction
//

class Subtraction : public Expr {
public:
  Subtraction(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Multiplication
//

class Multiplication : public Expr {
public:
  Multiplication(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Division
//

class Division : public Expr {
public:
  Division(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Remainder
//

class Remainder : public Expr {
public:
  Remainder(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Exponentiation
//

class Exponentiation : public Expr {
public:
  Exponentiation(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// ShiftLeft
//

class ShiftLeft : public Expr {
public:
  ShiftLeft(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// ShiftRight
//

class ShiftRight : public Expr {
public:
  ShiftRight(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// UnsignedShiftRight
//

class UnsignedShiftRight : public Expr {
public:
  UnsignedShiftRight(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// BitwiseNot
//

class BitwiseNot : public Expr {
public:
  BitwiseNot(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// BitwiseAnd
//

class BitwiseAnd : public Expr {
public:
  BitwiseAnd(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// BitwiseOr
//

class BitwiseOr : public Expr {
public:
  BitwiseOr(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// BitwiseXor
//

class BitwiseXor : public Expr {
public:
  BitwiseXor(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// LogicalNot
//

class LogicalNot : public Expr {
public:
  LogicalNot(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// LogicalAnd
//

class LogicalAnd : public Expr {
public:
  LogicalAnd(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// LogicalOr
//

class LogicalOr : public Expr {
public:
  LogicalOr(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// NullishCoalescing
//

class NullishCoalescing : public Expr {
public:
  NullishCoalescing(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Equality
//

class Equality : public Expr {
public:
  Equality(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Inequality
//

class Inequality : public Expr {
public:
  Inequality(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Identity
//

class Identity : public Expr {
public:
  Identity(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// Nonidentity
//

class Nonidentity : public Expr {
public:
  Nonidentity(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// GreaterThan
//

class GreaterThan : public Expr {
public:
  GreaterThan(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// GreaterThanOrEqual
//

class GreaterThanOrEqual : public Expr {
public:
  GreaterThanOrEqual(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// LessThan
//

class LessThan : public Expr {
public:
  LessThan(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// LessThanOrEqual
//

class LessThanOrEqual : public Expr {
public:
  LessThanOrEqual(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// In
//

class In : public Expr {
public:
  In(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
  PropertyCache m_cache;
};

//
// InstanceOf
//

class InstanceOf : public Expr {
public:
  InstanceOf(Expr *a, Expr *b) : m_a(a), m_b(b) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
};

//
// TypeOf
//

class TypeOf : public Expr {
public:
  enum class Type {
    Undefined,
    Boolean,
    Number,
    String,
    Object,
    Function,
  };

  TypeOf(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// PostIncrement
//

class PostIncrement : public Expr {
public:
  PostIncrement(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// PostDecrement
//

class PostDecrement : public Expr {
public:
  PostDecrement(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// PreIncrement
//

class PreIncrement : public Expr {
public:
  PreIncrement(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// PreDecrement
//

class PreDecrement : public Expr {
public:
  PreDecrement(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// Delete
//

class Delete : public Expr {
public:
  Delete(Expr *x) : m_x(x) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_x;
};

//
// Assignment
//

class Assignment : public Expr {
public:
  Assignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool is_argument() const override;
  virtual void to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const override;
  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual bool unpack(Context &ctx, Value &arg, int &var) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;

  friend class FunctionLiteral;
};

//
// AdditionAssignment
//

class AdditionAssignment : public Expr {
public:
  AdditionAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// SubtractionAssignment
//

class SubtractionAssignment : public Expr {
public:
  SubtractionAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// MultiplicationAssignment
//

class MultiplicationAssignment : public Expr {
public:
  MultiplicationAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// DivisionAssignment
//

class DivisionAssignment : public Expr {
public:
  DivisionAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// RemainderAssignment
//

class RemainderAssignment : public Expr {
public:
  RemainderAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// ExponentiationAssignment
//

class ExponentiationAssignment : public Expr {
public:
  ExponentiationAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// ShiftLeftAssignment
//

class ShiftLeftAssignment : public Expr {
public:
  ShiftLeftAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// ShiftRightAssignment
//

class ShiftRightAssignment : public Expr {
public:
  ShiftRightAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// UnsignedShiftRightAssignment
//

class UnsignedShiftRightAssignment : public Expr {
public:
  UnsignedShiftRightAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// BitwiseAndAssignment
//

class BitwiseAndAssignment : public Expr {
public:
  BitwiseAndAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// BitwiseOrAssignment
//

class BitwiseOrAssignment : public Expr {
public:
  BitwiseOrAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// BitwiseXorAssignment
//

class BitwiseXorAssignment : public Expr {
public:
  BitwiseXorAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// LogicalAndAssignment
//

class LogicalAndAssignment : public Expr {
public:
  LogicalAndAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// LogicalOrAssignment
//

class LogicalOrAssignment : public Expr {
public:
  LogicalOrAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// LogicalNullishAssignment
//

class LogicalNullishAssignment : public Expr {
public:
  LogicalNullishAssignment(Expr *l, Expr *r) : m_l(l), m_r(r) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_l;
  std::unique_ptr<Expr> m_r;
};

//
// Conditional
//

class Conditional : public Expr {
public:
  Conditional(Expr *a, Expr *b, Expr *c) : m_a(a), m_b(b), m_c(c) {}

  virtual bool eval(Context &ctx, Value &result) override;
  virtual void resolve(Context &ctx, int l, Imports *imports) override;
  virtual void dump(std::ostream &out, const std::string &indent) override;

private:
  std::unique_ptr<Expr> m_a;
  std::unique_ptr<Expr> m_b;
  std::unique_ptr<Expr> m_c;
};

} // namespace expr

//
// Expression constructors
//

inline Expr* discard(Expr *x) { return new expr::Discard(x); }
inline Expr* compound(Expr *list, Expr *append) { return new expr::Compound(list, append); }
inline Expr* concat(std::list<std::unique_ptr<Expr>> &&list) { return new expr::Concatenation(std::move(list)); }
inline Expr* undefined() { return new expr::Undefined; }
inline Expr* null() { return new expr::Null; }
inline Expr* boolean(bool b) { return new expr::BooleanLiteral(b); }
inline Expr* number(double n) { return new expr::NumberLiteral(n); }
inline Expr* string(const std::string &s) { return new expr::StringLiteral(s); }
inline Expr* object(std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> &list) { return new expr::ObjectLiteral(list); }
inline Expr* expand(Expr *x) { return new expr::ArrayExpansion(x); }
inline Expr* array(std::list<std::unique_ptr<Expr>> &&list) { return new expr::ArrayLiteral(std::move(list)); }
inline Expr* function(Expr *input, Expr *output) { return new expr::FunctionLiteral(input, output); }
inline Expr* global(const std::string &s) { return new expr::Global(s); }
inline Expr* local(const std::string &s) { return new expr::Local(s); }
inline Expr* identifier(const std::string &s) { return new expr::Identifier(s); }
inline Expr* prop(Expr *a, Expr *b) { return new expr::Property(a, b); }
inline Expr* construct(Expr *f) { return new expr::Construction(f); }
inline Expr* construct(Expr *f, std::vector<std::unique_ptr<Expr>> &&argv) { return new expr::Construction(f, std::move(argv)); }
inline Expr* call(Expr *f, std::vector<std::unique_ptr<Expr>> &&argv) { return new expr::Invocation(f, std::move(argv)); }
inline Expr* opt_prop(Expr *a, Expr *b) { return new expr::OptionalProperty(a, b); }
inline Expr* opt_call(Expr *f, std::vector<std::unique_ptr<Expr>> &&argv) { return new expr::OptionalInvocation(f, std::move(argv)); }
inline Expr* pos(Expr *x) { return new expr::Plus(x); }
inline Expr* neg(Expr *x) { return new expr::Negation(x); }
inline Expr* add(Expr *a, Expr *b) { return new expr::Addition(a, b); }
inline Expr* sub(Expr *a, Expr *b) { return new expr::Subtraction(a, b); }
inline Expr* mul(Expr *a, Expr *b) { return new expr::Multiplication(a, b); }
inline Expr* div(Expr *a, Expr *b) { return new expr::Division(a, b); }
inline Expr* rem(Expr *a, Expr *b) { return new expr::Remainder(a, b); }
inline Expr* pow(Expr *a, Expr *b) { return new expr::Exponentiation(a, b); }
inline Expr* shl(Expr *a, Expr *b) { return new expr::ShiftLeft(a, b); }
inline Expr* shr(Expr *a, Expr *b) { return new expr::ShiftRight(a, b); }
inline Expr* usr(Expr *a, Expr *b) { return new expr::UnsignedShiftRight(a, b); }
inline Expr* bit_not(Expr *x) { return new expr::BitwiseNot(x); }
inline Expr* bit_and(Expr *a, Expr *b) { return new expr::BitwiseAnd(a, b); }
inline Expr* bit_or(Expr *a, Expr *b) { return new expr::BitwiseOr(a, b); }
inline Expr* bit_xor(Expr *a, Expr *b) { return new expr::BitwiseXor(a, b); }
inline Expr* bool_not(Expr *x) { return new expr::LogicalNot(x); }
inline Expr* bool_and(Expr *a, Expr *b) { return new expr::LogicalAnd(a, b); }
inline Expr* bool_or(Expr *a, Expr *b) { return new expr::LogicalOr(a, b); }
inline Expr* null_or(Expr *a, Expr *b) { return new expr::NullishCoalescing(a, b); }
inline Expr* eql(Expr *a, Expr *b) { return new expr::Equality(a, b); }
inline Expr* neq(Expr *a, Expr *b) { return new expr::Inequality(a, b); }
inline Expr* same(Expr *a, Expr *b) { return new expr::Identity(a, b); }
inline Expr* diff(Expr *a, Expr *b) { return new expr::Nonidentity(a, b); }
inline Expr* gt(Expr *a, Expr *b) { return new expr::GreaterThan(a, b); }
inline Expr* ge(Expr *a, Expr *b) { return new expr::GreaterThanOrEqual(a, b); }
inline Expr* lt(Expr *a, Expr *b) { return new expr::LessThan(a, b); }
inline Expr* le(Expr *a, Expr *b) { return new expr::LessThanOrEqual(a, b); }
inline Expr* in(Expr *a, Expr *b) { return new expr::In(a, b); }
inline Expr* instance_of(Expr *a, Expr *b) { return new expr::InstanceOf(a, b); }
inline Expr* type_of(Expr *x) { return new expr::TypeOf(x); }
inline Expr* post_inc(Expr *x) { return new expr::PostIncrement(x); }
inline Expr* post_dec(Expr *x) { return new expr::PostDecrement(x); }
inline Expr* pre_inc(Expr *x) { return new expr::PreIncrement(x); }
inline Expr* pre_dec(Expr *x) { return new expr::PreDecrement(x); }
inline Expr* del(Expr *x) { return new expr::Delete(x); }
inline Expr* assign(Expr *l, Expr *r) { return new expr::Assignment(l, r); }
inline Expr* add_assign(Expr *l, Expr *r) { return new expr::AdditionAssignment(l, r); }
inline Expr* sub_assign(Expr *l, Expr *r) { return new expr::SubtractionAssignment(l, r); }
inline Expr* mul_assign(Expr *l, Expr *r) { return new expr::MultiplicationAssignment(l, r); }
inline Expr* div_assign(Expr *l, Expr *r) { return new expr::DivisionAssignment(l, r); }
inline Expr* rem_assign(Expr *l, Expr *r) { return new expr::RemainderAssignment(l, r); }
inline Expr* pow_assign(Expr *l, Expr *r) { return new expr::ExponentiationAssignment(l, r); }
inline Expr* shl_assign(Expr *l, Expr *r) { return new expr::ShiftLeftAssignment(l, r); }
inline Expr* shr_assign(Expr *l, Expr *r) { return new expr::ShiftRightAssignment(l, r); }
inline Expr* usr_assign(Expr *l, Expr *r) { return new expr::UnsignedShiftRightAssignment(l, r); }
inline Expr* bit_and_assign(Expr *l, Expr *r) { return new expr::BitwiseAndAssignment(l, r); }
inline Expr* bit_or_assign(Expr *l, Expr *r) { return new expr::BitwiseOrAssignment(l, r); }
inline Expr* bit_xor_assign(Expr *l, Expr *r) { return new expr::BitwiseXorAssignment(l, r); }
inline Expr* bool_and_assign(Expr *l, Expr *r) { return new expr::LogicalAndAssignment(l, r); }
inline Expr* bool_or_assign(Expr *l, Expr *r) { return new expr::LogicalOrAssignment(l, r); }
inline Expr* null_or_assign(Expr *l, Expr *r) { return new expr::LogicalNullishAssignment(l, r); }
inline Expr* select(Expr *a, Expr *b, Expr *c) { return new expr::Conditional(a, b, c); }

inline Expr* identifier_to_string(Expr *identifier) {
  auto i = dynamic_cast<expr::Identifier*>(identifier);
  return i ? i->to_string() : nullptr;
}

inline Expr* identifier_to_global(Expr *identifier) {
  auto i = dynamic_cast<expr::Identifier*>(identifier);
  return i ? i->to_global() : nullptr;
}

inline Expr* identifier_to_local(Expr *identifier) {
  auto i = dynamic_cast<expr::Identifier*>(identifier);
  return i ? i->to_local() : nullptr;
}

} // namespace pjs

#endif // PJS_EXPR_HPP
