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
// Expr::Imports
//

void Expr::Imports::add(Str *name, int file, Str *original_name) {
  m_imports[name] = { file, original_name };
}

bool Expr::Imports::get(Str *name, int *file, Str **original_name) {
  auto i = m_imports.find(name);
  if (i == m_imports.end()) return false;
  const auto &imp = i->second;
  *file = imp.first;
  *original_name = imp.second;
  return true;
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

void Discard::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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
  Reducer::Value *v[n];
  for (size_t i = 0; i < n; i++) {
    v[i] = m_exprs[i]->reduce(r);
  }
  return r.compound(v, n);
}

void Compound::resolve(Context &ctx, int l, Imports *imports) {
  for (const auto &p : m_exprs) {
    p->resolve(ctx, l, imports);
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

void Concatenation::resolve(Context &ctx, int l, Imports *imports) {
  for (const auto &p : m_exprs) {
    p->resolve(ctx, l, imports);
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
      e.index = m_class->find_field(s->s());
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
  unpack(args, vars);
}

void ObjectLiteral::unpack(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {
  for (const auto &e : m_entries) {
    e.value->unpack(args, vars);
  }
}

bool ObjectLiteral::unpack(Context &ctx, Value &arg, int &var) {
  auto obj = arg.to_object();
  if (!obj) return error(ctx, "cannot destructure null");
  for (const auto &e : m_entries) {
    if (auto *key = dynamic_cast<StringLiteral*>(e.key.get())) {
      Value val;
      obj->get(key->s(), val);
      if (!e.value->unpack(ctx, val, var)) {
        obj->release();
        return false;
      }
    }
  }
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

void ObjectLiteral::resolve(Context &ctx, int l, Imports *imports) {
  for (const auto &e : m_entries) {
    auto k = e.key.get();
    auto v = e.value.get();
    if (k) k->resolve(ctx, l, imports);
    if (v) v->resolve(ctx, l, imports);
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

void ArrayExpansion::resolve(Context &ctx, int l, Imports *imports) {
  m_array->resolve(ctx, l, imports);
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
  unpack(args, vars);
}

void ArrayLiteral::unpack(std::vector<Ref<Str>> &args, std::vector<Ref<Str>> &vars) const {
  for (const auto &i : m_list) {
    i->unpack(args, vars);
  }
}

bool ArrayLiteral::unpack(Context &ctx, Value &arg, int &var) {
  if (!arg.is_array()) return error(ctx, "cannot destructure");
  auto *a = arg.as<Array>();
  int i = 0;
  for (const auto &p : m_list) {
    Value val;
    a->get(i++, val);
    if (!p->unpack(ctx, val, var)) return false;
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
      Value a; if (!e->eval(ctx, a)) return false;
      if (a.is_string()) {
        return error(ctx, "TODO"); // TODO: expand a string
      } else if (a.is_array()) {
        auto arr = a.as<Array>();
        arr->iterate_all([&](Value &v, int) { obj->set(i++, v); });
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

void ArrayLiteral::resolve(Context &ctx, int l, Imports *imports) {
  for (const auto &p : m_list) {
    p->resolve(ctx, l, imports);
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

FunctionLiteral::FunctionLiteral(Expr *inputs, Expr *output) : m_output(output) {
  if (inputs) {
    if (auto comp = dynamic_cast<Compound*>(inputs)) {
      comp->break_down(m_inputs);
      delete comp;
    } else {
      m_inputs.push_back(std::unique_ptr<Expr>(inputs));
    }
    int i = 0;
    std::vector<Ref<Str>> args, vars;
    for (const auto &p : m_inputs) {
      p->to_arguments(args, vars);
      Parameter param;
      param.index = i++;
      if (auto *assign = dynamic_cast<Assignment*>(p.get())) {
        param.value = assign->m_r.get();
      }
      if (args.back() == Str::empty) {
        param.unpack = p.get();
      }
      m_parameters.push_back(param);
    }
    m_argc = args.size();
    m_variables.resize(m_argc + vars.size());
    for (size_t i = 0; i < m_variables.size(); i++) {
      m_variables[i].name = (i >= m_argc ? vars[i - m_argc] : args[i]);
    }
  }
}

bool FunctionLiteral::eval(Context &ctx, Value &result) {
  result.set(Function::make(m_method, nullptr, ctx.scope()));
  return true;
}

void FunctionLiteral::resolve(Context &ctx, int l, Imports *imports) {
  char name[100];
  std::sprintf(name, "(anonymous function at line %d column %d)", line(), column());
  m_method = Method::make(
    name,
    [this](Context &ctx, Object*, Value &result) {
      auto *scope = ctx.new_scope(m_argc, m_variables.size(), &m_variables[0]);
      int var_index = m_argc;
      for (const auto &p : m_parameters) {
        auto &arg = scope->value(p.index);
        if (arg.is_undefined()) {
          if (auto *v = p.value) {
            if (!v->eval(ctx, arg)) {
              return;
            }
          }
        }
        if (auto *e = p.unpack) {
          e->unpack(ctx, arg, var_index);
        }
      }
      m_output->eval(ctx, result);
      scope->clear();
    }
  );

  Context fctx(ctx, 0, nullptr, Scope::make(ctx.scope(), m_variables.size(), &m_variables[0]));
  for (auto &i : m_inputs) i->resolve(fctx, l, imports);
  m_output->resolve(fctx, l, imports);
}

auto FunctionLiteral::reduce(Reducer &r) -> Reducer::Value* {
  size_t argc = m_inputs.size();
  Expr *inputs[argc];
  for (size_t i = 0; i < argc; i++) inputs[i] = m_inputs[i].get();
  return r.function(argc, inputs, m_output.get());
}

void FunctionLiteral::dump(std::ostream &out, const std::string &indent) {
  out << indent << "function " << std::endl;
  for (const auto &p : m_inputs) {
    p->dump(out, indent + "  ");
  }
  m_output->dump(out, indent + "  ");
}

//
// Global
//

bool Global::is_left_value() const { return true; }

bool Global::eval(Context &ctx, Value &result) {
  m_cache.get(ctx.g(), m_key, result);
  return true;
}

bool Global::assign(Context &ctx, Value &value) {
  m_cache.set(ctx.g(), m_key, value);
  return true;
}

bool Global::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete a global variable");
}

void Global::dump(std::ostream &out, const std::string &indent) {
  out << indent << "global " << m_key->c_str() << std::endl;
}

//
// Local
//

bool Local::is_left_value() const { return true; }

bool Local::eval(Context &ctx, Value &result) {
  if (auto l = ctx.l(m_l)) {
    m_cache.get(l, m_key, result);
    return true;
  } else {
    return error(ctx, "no context");
  }
}

bool Local::assign(Context &ctx, Value &value) {
  if (auto l = ctx.l(m_l)) {
    m_cache.set(l, m_key, value);
    return true;
  } else {
    return error(ctx, "no context");
  }
}

bool Local::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete a local variable");
}

void Local::dump(std::ostream &out, const std::string &indent) {
  out << indent << "local " << m_key->c_str() << std::endl;
}

//
// Argument
//

bool Argument::is_left_value() const { return true; }

bool Argument::eval(Context &ctx, Value &result) {
  auto *scope = ctx.scope();
  for (int i = 0; i < m_level; i++) scope = scope->parent();
  result = scope->value(m_i);
  return true;
}

bool Argument::assign(Context &ctx, Value &value) {
  auto *scope = ctx.scope();
  for (int i = 0; i < m_level; i++) scope = scope->parent();
  scope->value(m_i) = value;
  return true;
}

bool Argument::clear(Context &ctx, Value &result) {
  return error(ctx, "cannot delete an argument");
}

void Argument::dump(std::ostream &out, const std::string &indent) {
  out << indent << "argument " << m_i << std::endl;
}

//
// Identifier
//

bool Identifier::is_left_value() const { return true; }
bool Identifier::is_argument() const { return true; }
void Identifier::to_arguments(std::vector<Ref<Str>> &args, std::vector<Ref<Str>>&) const { args.push_back(m_key); }
void Identifier::unpack(std::vector<Ref<Str>>&, std::vector<Ref<Str>> &vars) const { vars.push_back(m_key); }

bool Identifier::unpack(Context &ctx, Value &arg, int &var) {
  ctx.scope()->value(var++) = arg;
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

void Identifier::resolve(Context &ctx, int l, Imports *imports) {
  m_l = l;
  m_imports = imports;
  resolve(ctx);
}

void Identifier::resolve(Context &ctx) {
  auto *scope = ctx.scope();
  for (int level = 0; scope; scope = scope->parent(), level++) {
    if (auto *variables = scope->variables()) {
      for (int i = 0; i < scope->size(); i++) {
        if (variables[i].name == m_key) {
          m_resolved.reset(locate(new Argument(i, level)));
          if (scope != ctx.scope()) {
            variables[i].is_closure = true;
          }
          return;
        }
      }
    }
  }
  if (auto l = ctx.l(m_l)) {
    if (l->has(m_key)) {
      m_resolved.reset(locate(new Local(m_l, m_key)));
      return;
    }
  }
  if (m_imports) {
    int file;
    Str *key;
    if (m_imports->get(m_key, &file, &key)) {
      auto l = ctx.l(file);
      if (l->has(key)) {
        m_resolved.reset(locate(new Local(file, key)));
        return;
      }
    }
  }
  if (ctx.g()->has(m_key)) {
    m_resolved.reset(locate(new Global(m_key)));
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

void Property::resolve(Context &ctx, int l, Imports *imports) {
  m_obj->resolve(ctx, l, imports);
  m_key->resolve(ctx, l, imports);
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

void OptionalProperty::resolve(Context &ctx, int l, Imports *imports) {
  m_obj->resolve(ctx, l, imports);
  m_key->resolve(ctx, l, imports);
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
  Value f, argv[argc];
  if (!m_func->eval(ctx, f)) return false;
  if (!f.is_instance_of(class_of<Function>())) return error(ctx, "not a function");
  for (size_t i = 0; i < argc; i++) {
    if (!m_argv[i]->eval(ctx, argv[i])) return false;
  }
  result.set(f.as<Function>()->construct(ctx, argc, argv));
  if (ctx.ok()) return true;
  ctx.backtrace(source(), line(), column());
  return false;
}

void Construction::resolve(Context &ctx, int l, Imports *imports) {
  m_func->resolve(ctx, l, imports);
  for (const auto &p : m_argv) {
    p->resolve(ctx, l, imports);
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
  Value f, argv[argc];
  if (!m_func->eval(ctx, f)) return false;
  if (!f.is_function()) return error(ctx, "not a function");
  for (size_t i = 0; i < argc; i++) {
    if (!m_argv[i]->eval(ctx, argv[i])) return false;
  }
  ctx.trace(source(), line(), column());
  (*f.as<Function>())(ctx, argc, argv, result);
  if (ctx.ok()) return true;
  ctx.backtrace(source(), line(), column());
  return false;
}

void Invocation::resolve(Context &ctx, int l, Imports *imports) {
  m_func->resolve(ctx, l, imports);
  for (const auto &p : m_argv) {
    p->resolve(ctx, l, imports);
  }
}

auto Invocation::reduce(Reducer &r) -> Reducer::Value* {
  auto argc = m_argv.size();
  Reducer::Value *argv[argc];
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
  Value f, argv[argc];
  if (!m_func->eval(ctx, f)) return false;
  if (f.is_undefined() || f.is_null()) {
    result = Value::undefined;
    return true;
  }
  if (!f.is_class(class_of<Function>())) return error(ctx, "not a function");
  for (size_t i = 0; i < argc; i++) {
    if (!m_argv[i]->eval(ctx, argv[i])) return false;
  }
  (*f.as<Function>())(ctx, argc, argv, result);
  if (ctx.ok()) return true;
  ctx.backtrace(source(), line(), column());
  return false;
}

void OptionalInvocation::resolve(Context &ctx, int l, Imports *imports) {
  m_func->resolve(ctx, l, imports);
  for (const auto &p : m_argv) {
    p->resolve(ctx, l, imports);
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

void Plus::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void Negation::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void Addition::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Subtraction::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Multiplication::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Division::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Remainder::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Exponentiation::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void ShiftLeft::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void ShiftRight::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void UnsignedShiftRight::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void BitwiseNot::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void BitwiseAnd::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void BitwiseOr::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void BitwiseXor::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void LogicalNot::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void LogicalAnd::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void LogicalOr::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void NullishCoalescing::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Equality::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Inequality::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Identity::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void Nonidentity::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void GreaterThan::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void GreaterThanOrEqual::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void LessThan::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void LessThanOrEqual::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void In::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void InstanceOf::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
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

void TypeOf::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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
  }
  Value v(result.to_number() + 1);
  return m_x->assign(ctx, v);
}

void PostIncrement::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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
  }
  Value v(result.to_number() - 1);
  return m_x->assign(ctx, v);
}

void PostDecrement::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void PreIncrement::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void PreDecrement::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void Delete::resolve(Context &ctx, int l, Imports *imports) {
  m_x->resolve(ctx, l, imports);
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

void Assignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
}

bool Assignment::unpack(Context &ctx, Value &arg, int &var) {
  return m_l->unpack(ctx, arg, var);
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

void AdditionAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void SubtractionAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void MultiplicationAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void DivisionAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void RemainderAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void ExponentiationAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void ShiftLeftAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void ShiftRightAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void UnsignedShiftRightAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void BitwiseAndAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void BitwiseOrAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void BitwiseXorAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void LogicalAndAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void LogicalOrAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void LogicalNullishAssignment::resolve(Context &ctx, int l, Imports *imports) {
  m_l->resolve(ctx, l, imports);
  m_r->resolve(ctx, l, imports);
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

void Conditional::resolve(Context &ctx, int l, Imports *imports) {
  m_a->resolve(ctx, l, imports);
  m_b->resolve(ctx, l, imports);
  m_c->resolve(ctx, l, imports);
}

void Conditional::dump(std::ostream &out, const std::string &indent) {
  out << indent << "conditional" << std::endl;
  m_a->dump(out, indent + "  ");
  m_b->dump(out, indent + "  ");
  m_c->dump(out, indent + "  ");
}

} // namespace expr

} // namespace pjs
