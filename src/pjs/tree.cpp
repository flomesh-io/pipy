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

#include "tree.hpp"
#include "expr.hpp"
#include "module.hpp"

#include <algorithm>

namespace pjs {

//
// Tree::Scope
//

void Tree::Scope::declare_arg(Expr *expr) {
  if (!m_initialized) {
    auto index = m_args.size();
    auto unpack_index = m_vars.size();
    expr->to_arguments(m_args, m_vars);
    if (m_args.size() == index) return;
    InitArg init;
    init.index = index;
    if (auto assign = dynamic_cast<expr::Assignment*>(expr)) {
      init.default_value = assign->rvalue();
    }
    if (m_args[index] == Str::empty) {
      init.unpack = expr;
      init.unpack_index = unpack_index;
    }
    if (init.default_value || init.unpack) {
      m_init_args.push_back(init);
    }
  }
}

void Tree::Scope::declare_var(Str *name, Expr *value) {
  if (!m_initialized) {
    auto i = std::find(m_vars.begin(), m_vars.end(), name);
    if (i != m_vars.end()) {
      if (value) {
        InitVar init;
        init.index = i - m_vars.begin();
        init.value = value;
        m_init_vars.push_back(init);
      }
      return;
    }
    i = std::find(m_args.begin(), m_args.end(), name);
    if (i != m_args.end()) {
      if (value) {
        InitArg init;
        init.index = i - m_args.begin();
        init.value = value;
        m_init_args.push_back(init);
      }
    }
    if (value) {
      InitVar init;
      init.index = m_vars.size();
      init.value = value;
      m_init_vars.push_back(init);
    }
    m_vars.push_back(name);
  }
}

void Tree::Scope::declare_fiber_var(Str *name, Module *module) {
  if (!m_initialized) {
    if (std::find(m_vars.begin(), m_vars.end(), name) != m_vars.end()) return;
    if (std::find(m_args.begin(), m_args.end(), name) != m_args.end()) return;
    auto i = m_fiber_vars.find(name);
    if (i == m_fiber_vars.end()) {
      m_fiber_vars[name] = module->add_fiber_variable();
    }
  }
}

auto Tree::Scope::instantiate(Context &ctx) -> pjs::Scope* {
  init_variables();

  auto *scope = ctx.new_scope(m_args.size(), m_size, m_variables);

  // Initialize arguments
  for (const auto &init : m_init_args) {
    auto &arg = scope->value(init.index);
    if (auto v = init.value) { // Initialize locals
      if (!v->eval(ctx, arg)) {
        return nullptr;
      }
    } else if (arg.is_undefined()) { // Populate default values
      if (auto v = init.default_value) {
        if (!v->eval(ctx, arg)) {
          return nullptr;
        }
      }
    }
    if (auto v = init.unpack) { // Unpack objects
      auto index = init.unpack_index;
      if (!v->unpack(ctx, arg, ctx.scope()->values(), index)) {
        return nullptr;
      }
    }
  }

  // Initialize variables
  for (const auto &init : m_init_vars) {
    auto &var = scope->value(init.index);
    if (auto v = init.value) {
      if (!v->eval(ctx, var)) { // Initialize locals
        return nullptr;
      }
    }
  }
  return scope;
}

void Tree::Scope::init_variables() {
  if (!m_initialized) {
    m_size = m_args.size() + m_vars.size();
    m_variables.resize(m_size + m_fiber_vars.size());
    size_t i = 0;
    for (const auto &name : m_args) m_variables[i++].name = name;
    for (const auto &name : m_vars) m_variables[i++].name = name;
    for (auto &init : m_init_args) init.unpack_index += m_args.size();
    for (auto &init : m_init_vars) init.index += m_args.size();
    for (const auto &p : m_fiber_vars) {
      auto &v = m_variables[i++];
      v.name = p.first;
      v.index = p.second;
      v.is_fiber = true;
    }
    m_initialized = true;
  }
}

} // namespace pjs
