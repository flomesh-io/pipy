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

#ifndef API_CODEBASE_HPP
#define API_CODEBASE_HPP

#include "pjs/pjs.hpp"
#include "codebase.hpp"

namespace pipy {

//
// CodebaseWrapper
//

class CodebaseWrapper : public pjs::ObjectTemplate<CodebaseWrapper> {
public:
  static auto builtin() -> pjs::Array*;
  static auto builtin(pjs::Str *name) -> CodebaseWrapper*;

private:
  CodebaseWrapper();
  CodebaseWrapper(pjs::Str *path);
  ~CodebaseWrapper();

  pjs::Ref<pjs::Str> m_path;
  Codebase* m_codebase;

  friend class pjs::ObjectTemplate<CodebaseWrapper>;
};

} // namespace pipy

#endif // API_CODEBASE_HPP
