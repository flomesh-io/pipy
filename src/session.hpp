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

#ifndef SESSION_HPP
#define SESSION_HPP

#include "ns.hpp"
#include "object.hpp"

#include <functional>
#include <list>
#include <memory>

NS_BEGIN

class Context;
class Pipeline;
class Listener;
class Module;

//
// Session
//

class Session {
public:
  auto context() const -> Context& { return *m_context; }
  void input(std::unique_ptr<Object> obj);
  void output(Object::Receiver output) { m_output = output; }
  void free();

private:
  struct Chain {
    Chain* next;
    Module* module;
    Object::Receiver output;
  };

  Session(
    Pipeline *pipeline,
    const std::list<std::unique_ptr<Module>> &chain
  );

  ~Session();

  Pipeline* m_pipeline;
  Session* m_next = nullptr;
  std::shared_ptr<Context> m_context;
  Object::Receiver m_output;
  Chain* m_chain;
  uint64_t m_version = 0;

  friend class Pipeline;
};

NS_END

#endif // SESSION_HPP