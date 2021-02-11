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

#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "ns.hpp"
#include "module.hpp"

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>

NS_BEGIN

class Session;

//
// Pipeline
//

class Pipeline {
public:
  static auto get(const std::string &name) -> Pipeline*;
  static void add(const std::string &name, Pipeline *pipeline);
  static void remove(const std::string &name);

  static auto session_total() -> size_t { return s_session_total; }

  Pipeline(std::list<std::unique_ptr<Module>> &chain);

  auto alloc(std::shared_ptr<Context> ctx = nullptr) -> Session*;
  void update(std::list<std::unique_ptr<Module>> &chain);

private:
  static std::map<std::string, Pipeline*> s_named_pipelines;
  static size_t s_session_total;

  ~Pipeline();

  std::list<std::unique_ptr<Module>> m_chain;
  std::list<Session*> m_pool;
  uint64_t m_version = 0;
  size_t m_allocated = 0;
  size_t m_pooled = 0;
  bool m_removing = false;

  void free(Session *session);
  void clean();

  friend class Session;
};

NS_END

#endif // PIPELINE_HPP