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

#include "api/codebase-api.hpp"

namespace pipy {

auto CodebaseWrapper::builtin() -> pjs::Array* {
  auto a = pjs::Array::make();
  for (const auto &name : Codebase::list_builtin()) {
    a->push(pjs::Str::make(name));
  }
  return a;
}

auto CodebaseWrapper::builtin(pjs::Str *name) -> CodebaseWrapper* {
  return nullptr;
}

CodebaseWrapper::CodebaseWrapper()
  : m_codebase(Codebase::make())
{
}

CodebaseWrapper::CodebaseWrapper(pjs::Str *path)
  : m_path(path)
  , m_codebase(Codebase::from_fs(path->str()))
{
}

CodebaseWrapper::~CodebaseWrapper() {
  delete m_codebase;
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<CodebaseWrapper>::init() {
  ctor([](Context &ctx) -> Object* {
    if (ctx.argc() > 0) {
      Str *path;
      if (!ctx.arguments(1, &path)) return nullptr;
      return CodebaseWrapper::make(path);
    } else {
      return CodebaseWrapper::make();
    }
  });
}

template<> void ClassDef<Constructor<CodebaseWrapper>>::init() {
  ctor();

  method("builtin", [](Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() > 0) {
      Str *name;
      if (!ctx.arguments(1, &name)) return;
      ret.set(CodebaseWrapper::builtin(name));
    } else {
      ret.set(CodebaseWrapper::builtin());
    }
  });
}

} // namespace pjs
