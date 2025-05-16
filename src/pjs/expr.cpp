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

#include "expr.hpp"
#include "stmt.hpp"
#include "module.hpp"

namespace pjs {

//
// TypeOf::Type
//

template<> void pjs::EnumDef<expr::TypeOf::Type>::init() {
  define(expr::TypeOf::Type::Undefined, "undefined");
  define(expr::TypeOf::Type::Boolean, "boolean");
  define(expr::TypeOf::Type::Number, "number");
  define(expr::TypeOf::Type::String, "string");
  define(expr::TypeOf::Type::Object, "object");
  define(expr::TypeOf::Type::Function, "function");
}

//
// Expr
//

Expr::Expr()
{
}

Expr::~Expr()
{
}

namespace expr {

//
// Discard
//

bool Discard::eval(Context &ctx, Value &result) {
  if (!m_x->eval(ctx, result)) return false;
  result = Value::undefined;
  return true;
}

bool Discard::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void Discard::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void Discard::dump(std::ostream &out, const std::string &indent) {
  out << indent << "discard" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// Compound
//

bool Compound::is_argument_list() const {
  for (const auto &p : m_exprs) {
    if (!p->is_argument()) {
      return false;
    }
  }
  return true;
}

bool Compound::eval(Context &ctx, Value &result) {
  for (const auto &p : m_exprs) {
    result = Value::undefined;
    if (!p->eval(ctx, result)) {
      return false;
    }
  }
  return true;
}

auto Compound::reduce(Reducer &r) -> Reducer::Value* {
  size_t n = m_exprs.size();
  vl_array<Reducer::Value*> v(n);
  for (size_t i = 0; i < n; i++) {
    v[i] = m_exprs[i]->reduce(r);
  }
  return r.compound(v, n);
}

bool Compound::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  for (const auto &p : m_exprs) {
    if (!p->declare(module, scope, error)) return false;
  }
  return true;
}

void Compound::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  for (const auto &p : m_exprs) {
    p->resolve(module, ctx, imports);
  }
}

void Compound::dump(std::ostream &out, const std::string &indent) {
  out << indent << "compound" << std::endl;
  for (const auto &p : m_exprs) {
    p->dump(out, indent + "  ");
  }
}

//
// Concatenation
//

bool Concatenation::eval(Context &ctx, Value &result) {
  std::string str;
  for (const auto &p : m_exprs) {
    if (!p->eval(ctx, result)) {
      return false;
    }
    auto s = result.to_string();
    str += s->str();
    s->release();
  }
  result.set(str);
  return true;
}

bool Concatenation::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  for (const auto &p : m_exprs) {
    if (!p->declare(module, scope, error)) return false;
  }
  return true;
}

void Concatenation::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  for (const auto &p : m_exprs) {
    p->resolve(module, ctx, imports);
  }
}

void Concatenation::dump(std::ostream &out, const std::string &indent) {
  out << indent << "concatenation" << std::endl;
  for (const auto &p : m_exprs) {
    p->dump(out, indent + "  ");
  }
}

//
// Undefined
//

bool Undefined::eval(Context &ctx, Value &result) {
  result = Value::undefined;
  return true;
}

auto Undefined::reduce(Reducer &r) -> Reducer::Value* {
  return r.undefined();
}

void Undefined::dump(std::ostream &out, const std::string &indent) {
  out << indent << "undefined" << std::endl;
}

//
// Null
//

bool Null::eval(Context &ctx, Value &result) {
  result = Value::null;
  return true;
}

auto Null::reduce(Reducer &r) -> Reducer::Value* {
  return r.null();
}

void Null::dump(std::ostream &out, const std::string &indent) {
  out << indent << "null" << std::endl;
}

//
// BooleanLiteral
//

bool BooleanLiteral::eval(Context &ctx, Value &result) {
  result.set(m_b);
  return true;
}

auto BooleanLiteral::reduce(Reducer &r) -> Reducer::Value* {
  return r.boolean(m_b);
}

void BooleanLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << (m_b ? "true" : "false") << std::endl;
}

//
// NumberLiteral
//

bool NumberLiteral::eval(Context &ctx, Value &result) {
  result.set(m_n);
  return true;
}

auto NumberLiteral::reduce(Reducer &r) -> Reducer::Value* {
  return r.number(m_n);
}

void NumberLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << "number " << m_n << std::endl;
}

//
// StringLiteral
//

bool StringLiteral::eval(Context &ctx, Value &result) {
  result.set(m_s);
  return true;
}

auto StringLiteral::reduce(Reducer &r) -> Reducer::Value* {
  return r.string(m_s->str());
}

void StringLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << "string \"" << m_s->str() << '"' << std::endl;
}

//
// ObjectLiteral
//

ObjectLiteral::ObjectLiteral(std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> &entries) {
  std::list<Field*> fields;
  for (auto &e : entries) {
    Entry ent({
      -1,
      std::move(e.first),
      std::move(e.second),
    });
    if (auto *s = dynamic_cast<StringLiteral*>(ent.key.get())) {
      auto *f = Variable::make(s->s()->str(), Field::Enumerable | Field::Writable);
      fields.push_back(f);
    }
    m_entries.emplace_back(std::move(ent));
  }
  m_class = Class::make("", class_of<Object>(), fields);
  for (auto &e : m_entries) {
    if (auto *s = dynamic_cast<StringLiteral*>(e.key.get())) {
      auto f = m_class->field(m_class->find_field(s->s()));
      e.index = static_cast<Variable*>(f)->index();
    }
  }
}

bool ObjectLiteral::is_left_value() const {
  for (const auto &e : m_entries) {
    if (!dynamic_cast<StringLiteral*>(e.key.get())) return false;
    if (!e.value->is_left_value()) return false;
  }
  return true;
}

bool ObjectLiteral::is_argument() const {
  for (const auto &e : m_entries) {
    if (!dynamic_cast<StringLiteral*>(e.key.get())) return false;
    if (!e.value->is_argument()) return false;
  }
  return true;
}

void ObjectLiteral::to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {
  args.push_back(Str::empty);
  unpack(vars);
}

void ObjectLiteral::unpack(std::vector<Ref<Str>> &vars) const {
  for (const auto &e : m_entries) {
    e.value->unpack(vars);
  }
}

bool ObjectLiteral::unpack(Context &ctx, const Value &src, Value *dst, int &idx) {
  auto obj = src.to_object();
  if (!obj) return error(ctx, "cannot destruct null");
  for (const auto &e : m_entries) {
    if (auto *key = dynamic_cast<StringLiteral*>(e.key.get())) {
      Value val;
      obj->get(key->s(), val);
      if (!e.value->unpack(ctx, val, dst, idx)) {
        obj->release();
        return false;
      }
    }
  }
  obj->release();
  return true;
}

bool ObjectLiteral::eval(Context &ctx, Value &result) {
  auto obj = Object::make(m_class);
  auto data = obj->data();
  result.set(obj);
  for (const auto &e : m_entries) {
    auto k = e.key.get();
    auto v = e.value.get();
    if (e.index >= 0) {
      if (!v->eval(ctx, data->at(e.index))) return false;
    } else if (!k) {
      Value val; if (!v->eval(ctx, val)) return false;
      if (val.is_object()) {
        if (val.o()) Object::assign(obj, val.o());
      } else if (val.is_string()) {
        return error(ctx, "TODO");
      }
    } else {
      Value key; if (!k->eval(ctx, key)) return false;
      Value val; if (!v->eval(ctx, val)) return false;
      auto s = key.to_string();
      obj->ht_set(s, val);
      s->release();
    }
  }
  return true;
}

bool ObjectLiteral::assign(Context &ctx, Value &value) {
  int idx = 0;
  unpack(ctx, value, m_unpack_vals.data(), idx);
  for (size_t i = 0, n = m_unpack_vals.size(); i < n; i++) {
    if (!m_unpack_vars[i]->assign(ctx, m_unpack_vals[i])) {
      return false;
    }
  }
  return true;
}

bool ObjectLiteral::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (is_lval) {
    if (!is_left_value()) {
      error.tree = this;
      error.message = "illegal object destruction";
      return false;
    }
    std::vector<Ref<Str>> vars;
    unpack(vars);
    m_unpack_vars.resize(vars.size());
    m_unpack_vals.resize(vars.size());
    for (size_t i = 0; i < vars.size(); i++) {
      m_unpack_vars[i].reset(new Identifier(vars[i]->str()));
    }
    m_is_left_value = true;
  } else {
    for (const auto &e : m_entries) {
      auto k = e.key.get();
      auto v = e.value.get();
      if (k && !k->declare(module, scope, error)) return false;
      if (v && !v->declare(module, scope, error)) return false;
    }
  }
  return true;
}

void ObjectLiteral::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  if (m_is_left_value) {
    for (const auto &v : m_unpack_vars) {
      v->resolve(module, ctx, imports);
    }
  } else {
    for (const auto &e : m_entries) {
      auto k = e.key.get();
      auto v = e.value.get();
      if (k) k->resolve(module, ctx, imports);
      if (v) v->resolve(module, ctx, imports);
    }
  }
}

void ObjectLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << "object" << std::endl;
  auto indent_str = indent + "  ";
  for (const auto &e : m_entries) {
    if (e.key) {
      e.key->dump(out, indent_str);
    } else {
      out << indent_str << "..." << std::endl;
    }
    e.value->dump(out, indent_str);
  }
}

//
// ArrayExpansion
//

bool ArrayExpansion::eval(Context &ctx, Value &result) {
  return m_array->eval(ctx, result);
}

bool ArrayExpansion::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_array->declare(module, scope, error)) return false;
  return true;
}

void ArrayExpansion::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_array->resolve(module, ctx, imports);
}

void ArrayExpansion::dump(std::ostream &out, const std::string &indent) {
  out << indent << "expand" << std::endl;
  m_array->dump(out, indent + "  ");
}

//
// ArrayLiteral
//

bool ArrayLiteral::is_left_value() const {
  for (const auto &i : m_list) {
    if (!i->is_left_value()) return false;
  }
  return true;
}

bool ArrayLiteral::is_argument() const {
  for (const auto &i : m_list) {
    if (!i->is_argument()) return false;
  }
  return true;
}

void ArrayLiteral::to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {
  args.push_back(Str::empty);
  unpack(vars);
}

void ArrayLiteral::unpack(std::vector<Ref<Str>> &vars) const {
  for (const auto &i : m_list) {
    i->unpack(vars);
  }
}

bool ArrayLiteral::unpack(Context &ctx, const Value &src, Value *dst, int &idx) {
  if (!src.is_array()) return error(ctx, "cannot destruct");
  auto *a = src.as<Array>();
  int i = 0;
  for (const auto &p : m_list) {
    Value val;
    a->get(i++, val);
    if (!p->unpack(ctx, val, dst, idx)) return false;
  }
  return true;
}

bool ArrayLiteral::eval(Context &ctx, Value &result) {
  auto obj = Array::make(m_list.size());
  result.set(obj);
  int i = 0;
  for (const auto &p : m_list) {
    auto v = p.get();
    if (auto e = dynamic_cast<ArrayExpansion*>(v)) {
      Value v; if (!e->eval(ctx, v)) return false;
      if (v.is_string()) {
        auto s = v.s();
        auto n = s->length();
        if (n > 0) obj->set(i + n - 1, Value::undefined);
        Utf8Decoder decoder(
          [&](int c) {
            uint32_t code = c;
            obj->set(i++, Str::make(&code, 1));
          }
        );
        for (auto c : s->str()) decoder.input(c);
      } else if (v.is_array()) {
        auto a = v.as<Array>();
        auto n = a->length();
        if (n > 0) obj->set(i + n - 1, Value::undefined);
        for (int j = 0; j < n; j++) {
          Value v; a->get(j, v);
          obj->set(i++, v);
        }
      } else {
        return error(ctx, "object is not iterable");
      }
    } else {
      Value val; if (!v->eval(ctx, val)) return false;
      obj->set(i++, val);
    }
  }
  obj->length(i);
  return true;
}

bool ArrayLiteral::assign(Context &ctx, Value &value) {
  int idx = 0;
  unpack(ctx, value, m_unpack_vals.data(), idx);
  for (size_t i = 0, n = m_unpack_vals.size(); i < n; i++) {
    if (!m_unpack_vars[i]->assign(ctx, m_unpack_vals[i])) {
      return false;
    }
  }
  return true;
}

bool ArrayLiteral::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (is_lval) {
    if (!is_left_value()) {
      error.tree = this;
      error.message = "illegal array destruction";
      return false;
    }
    std::vector<Ref<Str>> vars;
    unpack(vars);
    m_unpack_vars.resize(vars.size());
    m_unpack_vals.resize(vars.size());
    for (size_t i = 0; i < vars.size(); i++) {
      m_unpack_vars[i].reset(new Identifier(vars[i]->str()));
    }
    m_is_left_value = true;
  } else {
    for (const auto &p : m_list) {
      if (!p->declare(module, scope, error)) return false;
    }
  }
  return true;
}

void ArrayLiteral::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  for (const auto &p : m_list) {
    p->resolve(module, ctx, imports);
  }
}

void ArrayLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << "array" << std::endl;
  auto indent_str = indent + "  ";
  for (const auto &p : m_list) {
    p->dump(out, indent_str);
  }
}

//
// FunctionLiteral
//

FunctionLiteral::FunctionLiteral(Expr *inputs, Expr *output)
  : FunctionLiteral(inputs, new stmt::Return(output)) {}

FunctionLiteral::FunctionLiteral(Expr *inputs, Stmt *output)
  : m_output(output)
  , m_scope(Scope::FUNCTION)
{
  if (inputs) {
    if (auto comp = dynamic_cast<Compound*>(inputs)) {
      comp->break_down(m_inputs);
      delete comp;
    } else {
      m_inputs.push_back(std::unique_ptr<Expr>(inputs));
    }
    for (const auto &input : m_inputs) {
      m_scope.declare_arg(input.get());
    }
  }
}

bool FunctionLiteral::eval(Context &ctx, Value &result) {
  result.set(Function::make(m_method, nullptr, ctx.scope()));
  return true;
}

bool FunctionLiteral::declare(Module *module, Scope &, Error &error, bool is_lval) {
  auto check_name = [&](pjs::Str *s) {
    if (!s) return true;
    if (s->str()[0] != '$') return true;
    if (s->length() == 1) return true;
    error.tree = this;
    error.message = "reserved argument name '" + s->str() + "'";
    return false;
  };
  for (const auto &arg : m_scope.args()) if (!check_name(arg)) return false;
  for (const auto &var : m_scope.vars()) if (!check_name(var)) return false;
  for (auto &i : m_inputs) i->declare(module, m_scope, error);
  return m_output->declare(module, m_scope, error);
}

void FunctionLiteral::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  char name[100];
  std::sprintf(name, "(anonymous function at line %d column %d)", line(), column());
  m_method = Method::make(
    name, [this](Context &ctx, Object*, Value &result) {
      auto scope = m_scope.instantiate(ctx);
      if (!scope) return;
      Stmt::Result res;
      m_output->execute(ctx, res);
      if (ctx.ok()) {
        if (res.is_return()) {
          result = res.value;
        } else {
          result = Value::undefined;
        }
      }
      scope->clear();
    }
  );

  Context fctx(ctx, 0, nullptr, pjs::Scope::make(ctx.instance(), ctx.scope(), m_scope.size(), m_scope.variables()));
  for (auto &i : m_inputs) i->resolve(module, fctx, imports);
  m_output->resolve(module, fctx, imports);
}

auto FunctionLiteral::reduce(Reducer &r) -> Reducer::Value* {
  size_t argc = m_inputs.size();
  vl_array<Expr*> inputs(argc);
  for (size_t i = 0; i < argc; i++) inputs[i] = m_inputs[i].get();
  if (auto ret = dynamic_cast<stmt::Return*>(m_output.get())) {
    return r.function(argc, inputs, ret->value());
  } else {
    return r.function(argc, inputs, m_output.get());
  }
}

void FunctionLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << "function" << std::endl;
  for (const auto &p : m_inputs) {
    p->dump(out, indent + "  ");
  }
  if (m_output) {
    m_output->dump(out, indent + "  ");
  }
}

//
// GlobalVariable
//

bool GlobalVariable::is_left_value() const { return true; }

bool GlobalVariable::eval(Context &ctx, Value &result) {
  m_cache.get(ctx.g(), m_key, result);
  return true;
}

bool GlobalVariable::assign(Context &ctx, Value &value) {
  m_cache.set(ctx.g(), m_key, value);
  return true;
}

bool GlobalVariable::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete a global variable");
}

void GlobalVariable::dump(std::ostream &out, const std::string &indent) {
  out << indent << "global-variable " << m_key->c_str() << std::endl;
}

//
// ImportedVariable
//

bool ImportedVariable::is_left_value() const { return true; }

bool ImportedVariable::eval(Context &ctx, Value &result) {
  m_import->get(result);
  return true;
}

bool ImportedVariable::assign(Context &ctx, Value &value) {
  return error(ctx, "cannot assign to an imported variable");
}

bool ImportedVariable::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete an imported variable");
}

void ImportedVariable::dump(std::ostream &out, const std::string &indent) {
  out << indent << "imported-variable " << m_import->alias->str() << std::endl;
}

//
// ExportedVariable
//

bool ExportedVariable::is_left_value() const { return true; }

bool ExportedVariable::eval(Context &ctx, Value &result) {
  auto obj = m_module->exports_object();
  obj->type()->get(obj, m_i, result);
  return true;
}

bool ExportedVariable::assign(Context &ctx, Value &value) {
  auto obj = m_module->exports_object();
  obj->type()->set(obj, m_i, value);
  return true;
}

bool ExportedVariable::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete an exported variable");
}

void ExportedVariable::dump(std::ostream &out, const std::string &indent) {
  out << indent << "exported-variable " << m_i << std::endl;
}

//
// LocalVariable
//

bool LocalVariable::is_left_value() const { return true; }

bool LocalVariable::eval(Context &ctx, Value &result) {
  auto *scope = ctx.scope();
  for (int i = 0; i < m_level; i++) scope = scope->parent();
  result = scope->value(m_i);
  return true;
}

bool LocalVariable::assign(Context &ctx, Value &value) {
  auto *scope = ctx.scope();
  for (int i = 0; i < m_level; i++) scope = scope->parent();
  scope->value(m_i) = value;
  return true;
}

bool LocalVariable::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete a local variable");
}

void LocalVariable::dump(std::ostream &out, const std::string &indent) {
  out << indent << "local-variable " << m_i << std::endl;
}

//
// FiberVariable
//

bool FiberVariable::is_left_value() const { return true; }

bool FiberVariable::eval(Context &ctx, Value &result) {
  auto fiber = ctx.root()->fiber();
  if (!fiber) {
    ctx.error("referencing fiber variable without a fiber");
    return false;
  }
  result = fiber->data(m_module->id())->at(m_i);
  return true;
}

bool FiberVariable::assign(Context &ctx, Value &value) {
  auto fiber = ctx.root()->fiber();
  if (!fiber) {
    ctx.error("referencing fiber variable without a fiber");
    return false;
  }
  fiber->data(m_module->id())->at(m_i) = value;
  return true;
}

bool FiberVariable::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete a fiber variable");
}

void FiberVariable::dump(std::ostream &out, const std::string &indent) {
  out << indent << "fiber-variable " << m_i << std::endl;
}

//
// Identifier
//

bool Identifier::is_left_value() const { return true; }
bool Identifier::is_argument() const { return true; }
void Identifier::to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>>&) const { args.push_back(m_key); }
void Identifier::unpack(std::vector<Ref<Str>> &vars) const { vars.push_back(m_key); }

bool Identifier::unpack(Context &ctx, const Value &src, Value *dst, int &idx) {
  dst[idx++] = src;
  return true;
}

bool Identifier::eval(Context &ctx, Value &result) {
  if (!m_resolved) resolve(ctx);
  if (!m_resolved) return error(ctx, "unresolved identifier");
  return m_resolved->eval(ctx, result);
}

bool Identifier::assign(Context &ctx, Value &value) {
  if (!m_resolved) resolve(ctx);
  if (!m_resolved) return error(ctx, "unresolved identifier");
  return m_resolved->assign(ctx, value);
}

bool Identifier::clear(Context &ctx, Value &result) {
  if (!m_resolved) resolve(ctx);
  if (!m_resolved) return error(ctx, "unresolved identifier");
  return m_resolved->clear(ctx, result);
}

void Identifier::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_imports = imports;
  m_module = module;
  resolve(ctx);
}

void Identifier::resolve(Context &ctx) {
  auto *scope = ctx.scope();
  for (int level = 0; scope; scope = scope->parent(), level++) {
    auto &variables = scope->variables();
    for (size_t i = 0, n = variables.size(); i < n; i++) {
      auto &v = variables[i];
      if (v.name == m_key) {
        if (v.is_fiber) {
          m_resolved.reset(locate(new FiberVariable(v.index, m_module)));
        } else {
          m_resolved.reset(locate(new LocalVariable(i, level)));
          if (scope != ctx.scope()) {
            v.is_closure = true;
          }
        }
        return;
      }
    }
  }

  if (m_module) {
    if (auto i = m_module->find_import(m_key)) {
      m_resolved.reset(locate(new ImportedVariable(i)));
      return;
    }
    int i = m_module->find_export(m_key);
    if (i >= 0) {
      m_resolved.reset(locate(new ExportedVariable(i, m_module)));
      return;
    }
  }

  if (ctx.g()->has(m_key)) {
    m_resolved.reset(locate(new GlobalVariable(m_key)));
    return;
  }
}

auto Identifier::reduce(Reducer &r) -> Reducer::Value* {
  return r.get(m_key->str());
}

void Identifier::dump(std::ostream &out, const std::string &indent) {
  out << indent << "identifier " << m_key->c_str() << std::endl;
}

//
// Property
//

bool Property::is_left_value() const { return true; }

bool Property::eval(Context &ctx, Value &result) {
  Value obj, key;
  if (!m_obj->eval(ctx, obj)) return false;
  if (!m_key->eval(ctx, key)) return false;
  if (obj.is_undefined()) return error(ctx, "cannot read property of undefined");
  if (obj.is_null()) return error(ctx, "cannot read property of null");
  auto o = obj.to_object();
  auto c = o->type();
  if (c->has_seti()) {
    auto i = key.to_number();
    if (std::isfinite(i)) {
      c->geti(o, i, result);
      o->release();
      return true;
    }
  }
  auto k = key.to_string();
  m_cache.get(o, k, result);
  k->release();
  o->release();
  return true;
}

bool Property::assign(Context &ctx, Value &value) {
  Value obj, key;
  if (!m_obj->eval(ctx, obj)) return false;
  if (!m_key->eval(ctx, key)) return false;
  if (obj.is_undefined()) return error(ctx, "cannot set property of undefined");
  if (obj.is_null()) return error(ctx, "cannot set property of null");
  auto o = obj.to_object();
  auto c = o->type();
  if (c->has_seti()) {
    auto i = key.to_number();
    if (std::isfinite(i)) {
      c->seti(o, i, value);
      o->release();
      return true;
    }
  }
  auto k = key.to_string();
  m_cache.set(o, k, value);
  k->release();
  o->release();
  return true;
}

bool Property::clear(Context &ctx, Value &result) {
  Value obj, key;
  if (!m_obj->eval(ctx, obj)) return false;
  if (!m_key->eval(ctx, key)) return false;
  if (obj.is_undefined()) return error(ctx, "cannot delete property of undefined");
  if (obj.is_null()) return error(ctx, "cannot delete property of null");
  auto o = obj.to_object();
  auto c = o->type();
  if (c->has_seti()) {
    auto i = key.to_number();
    if (std::isfinite(i)) {
      c->seti(o, i, Value::empty);
      o->release();
      result.set(true);
      return true;
    }
  }
  auto k = key.to_string();
  result.set(m_cache.del(o, k));
  k->release();
  o->release();
  return true;
}

bool Property::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_obj->declare(module, scope, error)) return false;
  if (!m_key->declare(module, scope, error)) return false;
  return true;
}

void Property::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_obj->resolve(module, ctx, imports);
  m_key->resolve(module, ctx, imports);
}

auto Property::reduce(Reducer &r) -> Reducer::Value* {
  return r.get(m_obj->reduce(r), m_key->reduce(r));
}

void Property::dump(std::ostream &out, const std::string &indent) {
  out << indent << "property" << std::endl;
  m_obj->dump(out, indent + "  ");
  m_key->dump(out, indent + "  ");
}

//
// OptionalProperty
//

bool OptionalProperty::eval(Context &ctx, Value &result) {
  Value obj, key;
  if (!m_obj->eval(ctx, obj)) return false;
  if (!m_key->eval(ctx, key)) return false;
  if (obj.is_undefined() || obj.is_null()) {
    result = Value::undefined;
    return true;
  }
  auto o = obj.to_object();
  auto c = o->type();
  if (c->has_seti()) {
    auto i = key.to_number();
    if (std::isfinite(i)) {
      c->geti(o, i, result);
      o->release();
      return true;
    }
  }
  auto k = key.to_string();
  m_cache.get(o, k, result);
  k->release();
  o->release();
  return true;
}

bool OptionalProperty::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_obj->declare(module, scope, error)) return false;
  if (!m_key->declare(module, scope, error)) return false;
  return true;
}

void OptionalProperty::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_obj->resolve(module, ctx, imports);
  m_key->resolve(module, ctx, imports);
}

void OptionalProperty::dump(std::ostream &out, const std::string &indent) {
  out << indent << "optional property" << std::endl;
  m_obj->dump(out, indent + "  ");
  m_key->dump(out, indent + "  ");
}

//
// Construction
//

bool Construction::eval(Context &ctx, Value &result) {
  auto argc = m_argv.size();
  vl_array<Value> argv(argc);
  Value f;
  if (!m_func->eval(ctx, f)) return false;
  if (!f.is_instance_of(class_of<Function>())) return error(ctx, "not a function");
  for (size_t i = 0; i < argc; i++) {
    if (!m_argv[i]->eval(ctx, argv[i])) return false;
  }
  ctx.trace(m_module, line(), column());
  result.set(f.as<Function>()->construct(ctx, argc, argv));
  if (ctx.ok()) return true;
  ctx.backtrace(source(), line(), column());
  return false;
}

bool Construction::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_func->declare(module, scope, error)) return false;
  for (const auto &p : m_argv) {
    if (!p->declare(module, scope, error)) return false;
  }
  return true;
}

void Construction::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_module = module;
  m_func->resolve(module, ctx, imports);
  for (const auto &p : m_argv) {
    p->resolve(module, ctx, imports);
  }
}

void Construction::dump(std::ostream &out, const std::string &indent) {
  out << indent << "construction" << std::endl;
  m_func->dump(out, indent + "  ");
  for (const auto &arg : m_argv) arg->dump(out, indent + "  ");
}

//
// Invocation
//

bool Invocation::eval(Context &ctx, Value &result) {
  auto argc = m_argv.size();
  vl_array<Value> argv(argc);
  Value f;
  if (!m_func->eval(ctx, f)) return false;
  if (!f.is_function()) return error(ctx, "not a function");
  for (size_t i = 0; i < argc; i++) {
    if (!m_argv[i]->eval(ctx, argv[i])) return false;
  }
  ctx.trace(m_module, line(), column());
  (*f.as<Function>())(ctx, argc, argv, result);
  if (ctx.ok()) return true;
  ctx.backtrace(source(), line(), column());
  return false;
}

bool Invocation::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_func->declare(module, scope, error)) return false;
  for (const auto &p : m_argv) {
    if (!p->declare(module, scope, error)) return false;
  }
  return true;
}

void Invocation::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_module = module;
  m_func->resolve(module, ctx, imports);
  for (const auto &p : m_argv) {
    p->resolve(module, ctx, imports);
  }
}

auto Invocation::reduce(Reducer &r) -> Reducer::Value* {
  auto argc = m_argv.size();
  vl_array<Reducer::Value*> argv(argc);
  for (int i = 0; i < argc; i++) {
    argv[i] = m_argv[i]->reduce(r);
  }
  return r.call(m_func->reduce(r), argv, argc);
}

void Invocation::dump(std::ostream &out, const std::string &indent) {
  out << indent << "invocation" << std::endl;
  m_func->dump(out, indent + "  ");
  for (const auto &arg : m_argv) arg->dump(out, indent + "  ");
}

//
// OptionalInvocation
//

bool OptionalInvocation::eval(Context &ctx, Value &result) {
  auto argc = m_argv.size();
  vl_array<Value> argv(argc);
  Value f;
  if (!m_func->eval(ctx, f)) return false;
  if (f.is_undefined() || f.is_null()) {
    result = Value::undefined;
    return true;
  }
  if (!f.is_class(class_of<Function>())) return error(ctx, "not a function");
  for (size_t i = 0; i < argc; i++) {
    if (!m_argv[i]->eval(ctx, argv[i])) return false;
  }
  ctx.trace(m_module, line(), column());
  (*f.as<Function>())(ctx, argc, argv, result);
  if (ctx.ok()) return true;
  ctx.backtrace(source(), line(), column());
  return false;
}

bool OptionalInvocation::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_func->declare(module, scope, error)) return false;
  for (const auto &p : m_argv) {
    if (!p->declare(module, scope, error)) return false;
  }
  return true;
}

void OptionalInvocation::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_module = module;
  m_func->resolve(module, ctx, imports);
  for (const auto &p : m_argv) {
    p->resolve(module, ctx, imports);
  }
}

void OptionalInvocation::dump(std::ostream &out, const std::string &indent) {
  out << indent << "optional invocation" << std::endl;
  m_func->dump(out, indent + "  ");
  for (const auto &arg : m_argv) arg->dump(out, indent + "  ");
}

//
// Plus
//

bool Plus::eval(Context &ctx, Value &result) {
  Value x;
  if (!m_x->eval(ctx, x)) return false;
  result.set(x.to_number());
  return true;
}

bool Plus::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void Plus::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void Plus::dump(std::ostream &out, const std::string &indent) {
  out << indent << "plus" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// Negation
//

bool Negation::eval(Context &ctx, Value &result) {
  Value x;
  if (!m_x->eval(ctx, x)) return false;
  if (x.is<Int>()) {
    result.set(x.as<Int>()->neg());
    return true;
  }
  result.set(-x.to_number());
  return true;
}

bool Negation::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void Negation::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void Negation::dump(std::ostream &out, const std::string &indent) {
  out << indent << "negation" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// Addition
//

bool Addition::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is_string() || b.is_string()) {
    auto sa = a.to_string();
    auto sb = b.to_string();
    result.set(sa->str() + sb->str());
    sa->release();
    sb->release();
    return true;
  }
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->add(ib));
    ia->release();
    ib->release();
    return true;
  }
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(na + nb);
  return true;
}

bool Addition::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Addition::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Addition::dump(std::ostream &out, const std::string &indent) {
  out << indent << "addition" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Subtraction
//

bool Subtraction::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->sub(ib));
    ia->release();
    ib->release();
    return true;
  }
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(na - nb);
  return true;
}

bool Subtraction::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Subtraction::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Subtraction::dump(std::ostream &out, const std::string &indent) {
  out << indent << "subtraction" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Multiplication
//

bool Multiplication::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->mul(ib));
    ia->release();
    ib->release();
    return true;
  }
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(na * nb);
  return true;
}

bool Multiplication::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Multiplication::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Multiplication::dump(std::ostream &out, const std::string &indent) {
  out << indent << "multiplication" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Division
//

bool Division::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->div(ib));
    ia->release();
    ib->release();
    return true;
  }
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(na / nb);
  return true;
}

bool Division::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Division::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Division::dump(std::ostream &out, const std::string &indent) {
  out << indent << "division" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Remainder
//

bool Remainder::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->mod(ib));
    ia->release();
    ib->release();
    return true;
  }
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(std::fmod(na, nb));
  return true;
}

bool Remainder::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Remainder::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Remainder::dump(std::ostream &out, const std::string &indent) {
  out << indent << "remainder" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Exponentiation
//

bool Exponentiation::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(std::pow(na, nb));
  return true;
}

bool Exponentiation::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Exponentiation::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Exponentiation::dump(std::ostream &out, const std::string &indent) {
  out << indent << "exponentiation" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// ShiftLeft
//

bool ShiftLeft::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>()) {
    result.set(a.as<Int>()->shl(b.to_int32()));
    return true;
  }
  int32_t na = a.to_int32();
  int32_t nb = b.to_int32();
  result.set(na << nb);
  return true;
}

bool ShiftLeft::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void ShiftLeft::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void ShiftLeft::dump(std::ostream &out, const std::string &indent) {
  out << indent << "shift left" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// ShiftRight
//

bool ShiftRight::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>()) {
    result.set(a.as<Int>()->shr(b.to_int32()));
    return true;
  }
  int32_t na = a.to_int32();
  int32_t nb = b.to_int32();
  result.set(na >> nb);
  return true;
}

bool ShiftRight::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void ShiftRight::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void ShiftRight::dump(std::ostream &out, const std::string &indent) {
  out << indent << "shift right" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// UnsignedShiftRight
//

bool UnsignedShiftRight::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>()) {
    result.set(a.as<Int>()->bitwise_shr(b.to_int32()));
    return true;
  }
  int32_t na = a.to_int32();
  int32_t nb = b.to_int32();
  result.set((uint32_t)na >> nb);
  return true;
}

bool UnsignedShiftRight::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void UnsignedShiftRight::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void UnsignedShiftRight::dump(std::ostream &out, const std::string &indent) {
  out << indent << "unsigned shift right" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// BitwiseNot
//

bool BitwiseNot::eval(Context &ctx, Value &result) {
  Value x;
  if (!m_x->eval(ctx, x)) return false;
  if (x.is<Int>()) {
    result.set(x.as<Int>()->bitwise_not());
    return true;
  }
  result.set(~x.to_int32());
  return true;
}

bool BitwiseNot::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void BitwiseNot::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void BitwiseNot::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise not" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// BitwiseAnd
//

bool BitwiseAnd::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->bitwise_and(ib));
    ia->release();
    ib->release();
    return true;
  }
  int32_t na = a.to_int32();
  int32_t nb = b.to_int32();
  result.set(na & nb);
  return true;
}

bool BitwiseAnd::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void BitwiseAnd::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void BitwiseAnd::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise and" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// BitwiseOr
//

bool BitwiseOr::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->bitwise_or(ib));
    ia->release();
    ib->release();
    return true;
  }
  int32_t na = a.to_int32();
  int32_t nb = b.to_int32();
  result.set(na | nb);
  return true;
}

bool BitwiseOr::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void BitwiseOr::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void BitwiseOr::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise or" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// BitwiseXor
//

bool BitwiseXor::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->bitwise_xor(ib));
    ia->release();
    ib->release();
    return true;
  }
  int32_t na = a.to_int32();
  int32_t nb = b.to_int32();
  result.set(na ^ nb);
  return true;
}

bool BitwiseXor::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void BitwiseXor::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void BitwiseXor::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise xor" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// LogicalNot
//

bool LogicalNot::eval(Context &ctx, Value &result) {
  Value x;
  if (!m_x->eval(ctx, x)) return false;
  result.set(!x.to_boolean());
  return true;
}

bool LogicalNot::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void LogicalNot::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void LogicalNot::dump(std::ostream &out, const std::string &indent) {
  out << indent << "logical not" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// LogicalAnd
//

bool LogicalAnd::eval(Context &ctx, Value &result) {
  if (!m_a->eval(ctx, result)) return false;
  if (!result.to_boolean()) return true;
  if (!m_b->eval(ctx, result)) return false;
  return true;
}

bool LogicalAnd::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void LogicalAnd::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void LogicalAnd::dump(std::ostream &out, const std::string &indent) {
  out << indent << "logical and" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// LogicalOr
//

bool LogicalOr::eval(Context &ctx, Value &result) {
  if (!m_a->eval(ctx, result)) return false;
  if (result.to_boolean()) return true;
  if (!m_b->eval(ctx, result)) return false;
  return true;
}

bool LogicalOr::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void LogicalOr::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void LogicalOr::dump(std::ostream &out, const std::string &indent) {
  out << indent << "logical or" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// NullishCoalescing
//

bool NullishCoalescing::eval(Context &ctx, Value &result) {
  if (!m_a->eval(ctx, result)) return false;
  if (!result.is_undefined() && !result.is_null()) return true;
  if (!m_b->eval(ctx, result)) return false;
  return true;
}

bool NullishCoalescing::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void NullishCoalescing::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void NullishCoalescing::dump(std::ostream &out, const std::string &indent) {
  out << indent << "nullish coalescing" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Equality
//

bool Equality::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->eql(ib));
    ia->release();
    ib->release();
    return true;
  }
  result.set(Value::is_equal(a, b));
  return true;
}

bool Equality::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Equality::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Equality::dump(std::ostream &out, const std::string &indent) {
  out << indent << "equality" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Inequality
//

bool Inequality::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(!ia->eql(ib));
    ia->release();
    ib->release();
    return true;
  }
  result.set(!Value::is_equal(a, b));
  return true;
}

bool Inequality::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Inequality::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Inequality::dump(std::ostream &out, const std::string &indent) {
  out << indent << "inequality" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Identity
//

bool Identity::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  result.set(Value::is_identical(a, b));
  return true;
}

bool Identity::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Identity::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Identity::dump(std::ostream &out, const std::string &indent) {
  out << indent << "identity" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// Nonidentity
//

bool Nonidentity::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  result.set(!Value::is_identical(a, b));
  return true;
}

bool Nonidentity::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void Nonidentity::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void Nonidentity::dump(std::ostream &out, const std::string &indent) {
  out << indent << "nonidentity" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// GreaterThan
//

bool GreaterThan::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is_undefined() || b.is_undefined()) {
    result.set(false);
  } else if (a.is_string() && b.is_string()) {
    result.set(a.s()->str() > b.s()->str());
  } else if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->cmp(ib) > 0);
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na > nb);
  }
  return true;
}

bool GreaterThan::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void GreaterThan::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void GreaterThan::dump(std::ostream &out, const std::string &indent) {
  out << indent << "greater than" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// GreaterThanOrEqual
//

bool GreaterThanOrEqual::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is_undefined() || b.is_undefined()) {
    result.set(false);
  } else if (a.is_string() && b.is_string()) {
    result.set(a.s()->str() >= b.s()->str());
  } else if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->cmp(ib) >= 0);
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na >= nb);
  }
  return true;
}

bool GreaterThanOrEqual::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void GreaterThanOrEqual::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void GreaterThanOrEqual::dump(std::ostream &out, const std::string &indent) {
  out << indent << "greater than or equal" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// LessThan
//

bool LessThan::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is_undefined() || b.is_undefined()) {
    result.set(false);
  } else if (a.is_string() && b.is_string()) {
    result.set(a.s()->str() < b.s()->str());
  } else if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->cmp(ib) < 0);
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na < nb);
  }
  return true;
}

bool LessThan::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void LessThan::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void LessThan::dump(std::ostream &out, const std::string &indent) {
  out << indent << "less than" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// LessThanOrEqual
//

bool LessThanOrEqual::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (a.is_undefined() || b.is_undefined()) {
    result.set(false);
  } else if (a.is_string() && b.is_string()) {
    result.set(a.s()->str() <= b.s()->str());
  } else if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->cmp(ib) <= 0);
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na <= nb);
  }
  return true;
}

bool LessThanOrEqual::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void LessThanOrEqual::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void LessThanOrEqual::dump(std::ostream &out, const std::string &indent) {
  out << indent << "less than or equal" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// In
//

bool In::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (!b.is_object()) return error(ctx, "cannot use 'in' operator on non-objects");
  auto c = b.o()->type();
  if (c->has_geti()) {
    // TODO: Handle arrays
    return error(ctx, "TODO: Handle arrays");
  } else {
    auto s = a.to_string();
    result.set(m_cache.has(b.o(), s));
    s->release();
  }
  return true;
}

bool In::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void In::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void In::dump(std::ostream &out, const std::string &indent) {
  out << indent << "in" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// InstanceOf
//

bool InstanceOf::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_a->eval(ctx, a)) return false;
  if (!m_b->eval(ctx, b)) return false;
  if (!b.is_function()) return error(ctx, "right-hand side of 'instanceof' is not callable");
  auto f = b.as<Function>();
  auto m = f->method();
  auto c = m->constructor_class();
  if (!c) return error(ctx, "right-hand side of 'instanceof' is not a constructor");
  if (!a.is_object() || !a.o()) {
    result.set(false);
  } else {
    result.set(a.o()->type()->is_derived_from(c));
  }
  return true;
}

bool InstanceOf::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  return true;
}

void InstanceOf::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
}

void InstanceOf::dump(std::ostream &out, const std::string &indent) {
  out << indent << "instance of" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
}

//
// TypeOf
//

bool TypeOf::eval(Context &ctx, Value &result) {
  Value x;
  if (!m_x->eval(ctx, x)) return false;
  switch (x.type()) {
    case Value::Type::Empty: result.set(EnumDef<Type>::name(Type::Undefined)); break;
    case Value::Type::Undefined: result.set(EnumDef<Type>::name(Type::Undefined)); break;
    case Value::Type::Boolean: result.set(EnumDef<Type>::name(Type::Boolean)); break;
    case Value::Type::Number: result.set(EnumDef<Type>::name(Type::Number)); break;
    case Value::Type::String: result.set(EnumDef<Type>::name(Type::String)); break;
    case Value::Type::Object:
      result.set(x.is_function()
        ? EnumDef<Type>::name(Type::Function)
        : EnumDef<Type>::name(Type::Object)
      );
      break;
  }
  return true;
}

bool TypeOf::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void TypeOf::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void TypeOf::dump(std::ostream &out, const std::string &indent) {
  out << indent << "type of" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// PostIncrement
//

bool PostIncrement::eval(Context &ctx, Value &result) {
  if (!m_x->eval(ctx, result)) return false;
  if (result.is<Int>()) {
    Value v(result.as<Int>()->inc());
    return m_x->assign(ctx, v);
  } else if (!result.is_number()) {
    result.set(result.to_number());
  }
  Value v(result.n() + 1);
  return m_x->assign(ctx, v);
}

bool PostIncrement::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void PostIncrement::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void PostIncrement::dump(std::ostream &out, const std::string &indent) {
  out << indent << "post increment" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// PostDecrement
//

bool PostDecrement::eval(Context &ctx, Value &result) {
  if (!m_x->eval(ctx, result)) return false;
  if (result.is<Int>()) {
    Value v(result.as<Int>()->dec());
    return m_x->assign(ctx, v);
  } else if (!result.is_number()) {
    result.set(result.to_number());
  }
  Value v(result.n() - 1);
  return m_x->assign(ctx, v);
}

bool PostDecrement::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void PostDecrement::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void PostDecrement::dump(std::ostream &out, const std::string &indent) {
  out << indent << "post decrement" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// PreIncrement
//

bool PreIncrement::eval(Context &ctx, Value &result) {
  if (!m_x->eval(ctx, result)) return false;
  if (result.is<Int>()) {
    result.set(result.as<Int>()->inc());
  } else {
    result.set(result.to_number() + 1);
  }
  return m_x->assign(ctx, result);
}

bool PreIncrement::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void PreIncrement::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void PreIncrement::dump(std::ostream &out, const std::string &indent) {
  out << indent << "pre increment" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// PreDecrement
//

bool PreDecrement::eval(Context &ctx, Value &result) {
  if (!m_x->eval(ctx, result)) return false;
  if (result.is<Int>()) {
    result.set(result.as<Int>()->dec());
  } else {
    result.set(result.to_number() - 1);
  }
  return m_x->assign(ctx, result);
}

bool PreDecrement::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void PreDecrement::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void PreDecrement::dump(std::ostream &out, const std::string &indent) {
  out << indent << "pre decrement" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// Delete
//

bool Delete::eval(Context &ctx, Value &result) {
  return m_x->clear(ctx, result);
}

bool Delete::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  return m_x->declare(module, scope, error);
}

void Delete::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_x->resolve(module, ctx, imports);
}

void Delete::dump(std::ostream &out, const std::string &indent) {
  out << indent << "delete" << std::endl;
  m_x->dump(out, indent + "  ");
}

//
// Assignment
//

bool Assignment::is_argument() const {
  return m_l->is_argument();
}

void Assignment::to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {
  m_l->to_arguments(args, vars);
}

bool Assignment::eval(Context &ctx, Value &result) {
  if (!m_r->eval(ctx, result)) return false;
  return m_l->assign(ctx, result);
}

bool Assignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error, true)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void Assignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

bool Assignment::unpack(Context &ctx, const Value &src, Value *dst, int &idx) {
  return m_l->unpack(ctx, src, dst, idx);
}

void Assignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// AdditionAssignment
//

bool AdditionAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is_string() || b.is_string()) {
    auto sa = a.to_string();
    auto sb = b.to_string();
    result.set(sa->str() + sb->str());
    sa->release();
    sb->release();
  } else if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->add(ib));
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na + nb);
  }
  return m_l->assign(ctx, result);
}

bool AdditionAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void AdditionAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void AdditionAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "addition assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// SubtractionAssignment
//

bool SubtractionAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->sub(ib));
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na - nb);
  }
  return m_l->assign(ctx, result);
}

bool SubtractionAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void SubtractionAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void SubtractionAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "subtraction assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// MultiplicationAssignment
//

bool MultiplicationAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->mul(ib));
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na * nb);
  }
  return m_l->assign(ctx, result);
}

bool MultiplicationAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void MultiplicationAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void MultiplicationAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "multiplication assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// DivisionAssignment
//

bool DivisionAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->div(ib));
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(na / nb);
  }
  return m_l->assign(ctx, result);
}

bool DivisionAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void DivisionAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void DivisionAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "division assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// RemainderAssignment
//

bool RemainderAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->mod(ib));
    ia->release();
    ib->release();
  } else {
    auto na = a.to_number();
    auto nb = b.to_number();
    result.set(std::fmod(na, nb));
  }
  return m_l->assign(ctx, result);
}

bool RemainderAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void RemainderAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void RemainderAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "remainder assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// ExponentiationAssignment
//

bool ExponentiationAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  auto na = a.to_number();
  auto nb = b.to_number();
  result.set(std::pow(na, nb));
  return m_l->assign(ctx, result);
}

bool ExponentiationAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void ExponentiationAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void ExponentiationAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "exponentiation assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// ShiftLeftAssignment
//

bool ShiftLeftAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>()) {
    result.set(a.as<Int>()->shl(b.to_int32()));
  } else {
    int32_t na(a.to_number());
    int32_t nb(b.to_number());
    result.set(na << nb);
  }
  return m_l->assign(ctx, result);
}

bool ShiftLeftAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void ShiftLeftAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void ShiftLeftAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "shift left assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// ShiftRightAssignment
//

bool ShiftRightAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>()) {
    result.set(a.as<Int>()->shr(b.to_int32()));
  } else {
    int32_t na(a.to_number());
    int32_t nb(b.to_number());
    result.set(na >> nb);
  }
  return m_l->assign(ctx, result);
}

bool ShiftRightAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void ShiftRightAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void ShiftRightAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "shift right assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// UnsignedShiftRightAssignment
//

bool UnsignedShiftRightAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>()) {
    result.set(a.as<Int>()->bitwise_shr(b.to_int32()));
  } else {
    int32_t na(a.to_number());
    int32_t nb(b.to_number());
    result.set((uint32_t)na >> nb);
  }
  return m_l->assign(ctx, result);
}

bool UnsignedShiftRightAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void UnsignedShiftRightAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void UnsignedShiftRightAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "unsigned shift right assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// BitwiseAndAssignment
//

bool BitwiseAndAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->bitwise_and(ib));
    ia->release();
    ib->release();
  } else {
    int32_t na(a.to_number());
    int32_t nb(b.to_number());
    result.set(na & nb);
  }
  return m_l->assign(ctx, result);
}

bool BitwiseAndAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void BitwiseAndAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void BitwiseAndAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise and assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// BitwiseOrAssignment
//

bool BitwiseOrAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->bitwise_or(ib));
    ia->release();
    ib->release();
  } else {
    int32_t na(a.to_number());
    int32_t nb(b.to_number());
    result.set(na | nb);
  }
  return m_l->assign(ctx, result);
}

bool BitwiseOrAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void BitwiseOrAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void BitwiseOrAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise or assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// BitwiseXorAssignment
//

bool BitwiseXorAssignment::eval(Context &ctx, Value &result) {
  Value a, b;
  if (!m_l->eval(ctx, a)) return false;
  if (!m_r->eval(ctx, b)) return false;
  if (a.is<Int>() || b.is<Int>()) {
    auto ia = a.to_int();
    auto ib = b.to_int();
    result.set(ia->bitwise_xor(ib));
    ia->release();
    ib->release();
  } else {
    int32_t na(a.to_number());
    int32_t nb(b.to_number());
    result.set(na ^ nb);
  }
  return m_l->assign(ctx, result);
}

bool BitwiseXorAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void BitwiseXorAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void BitwiseXorAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "bitwise xor assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// LogicalAndAssignment
//

bool LogicalAndAssignment::eval(Context &ctx, Value &result) {
  if (!m_l->eval(ctx, result)) return false;
  if (!result.to_boolean()) return true;
  if (!m_r->eval(ctx, result)) return false;
  return m_l->assign(ctx, result);
}

bool LogicalAndAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void LogicalAndAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void LogicalAndAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "logical and assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// LogicalOrAssignment
//

bool LogicalOrAssignment::eval(Context &ctx, Value &result) {
  if (!m_l->eval(ctx, result)) return false;
  if (result.to_boolean()) return true;
  if (!m_r->eval(ctx, result)) return false;
  return m_l->assign(ctx, result);
}

bool LogicalOrAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void LogicalOrAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void LogicalOrAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "logical or assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// LogicalNullishAssignment
//

bool LogicalNullishAssignment::eval(Context &ctx, Value &result) {
  if (!m_l->eval(ctx, result)) return false;
  if (!result.is_undefined() && !result.is_null()) return true;
  if (!m_r->eval(ctx, result)) return false;
  return m_l->assign(ctx, result);
}

bool LogicalNullishAssignment::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_l->declare(module, scope, error)) return false;
  if (!m_r->declare(module, scope, error)) return false;
  return true;
}

void LogicalNullishAssignment::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_l->resolve(module, ctx, imports);
  m_r->resolve(module, ctx, imports);
}

void LogicalNullishAssignment::dump(std::ostream &out, const std::string &indent) {
  out << indent << "logical nullish assignment" << std::endl;
  m_l->dump(out, indent + "  ");
  m_r->dump(out, indent + "  ");
}

//
// Conditional
//

bool Conditional::eval(Context &ctx, Value &result) {
  Value cond;
  if (!m_a->eval(ctx, cond)) return false;
  if (cond.to_boolean()) {
    return m_b->eval(ctx, result);
  } else {
    return m_c->eval(ctx, result);
  }
}

bool Conditional::declare(Module *module, Scope &scope, Error &error, bool is_lval) {
  if (!m_a->declare(module, scope, error)) return false;
  if (!m_b->declare(module, scope, error)) return false;
  if (!m_c->declare(module, scope, error)) return false;
  return true;
}

void Conditional::resolve(Module *module, Context &ctx, LegacyImports *imports) {
  m_a->resolve(module, ctx, imports);
  m_b->resolve(module, ctx, imports);
  m_c->resolve(module, ctx, imports);
}

void Conditional::dump(std::ostream &out, const std::string &indent) {
  out << indent << "conditional" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
  m_c->dump(out, indent + "  ");
}

} // namespace expr

} // namespace pjs
