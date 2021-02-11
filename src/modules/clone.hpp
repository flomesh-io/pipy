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

#ifndef CLONE_HPP
#define CLONE_HPP

#include "module.hpp"

#include <list>

NS_BEGIN

class Session;

//
// Clone
//

class Clone : public Module {
public:
  Clone();

private:
  ~Clone();

  virtual auto help() -> std::list<std::string> override;
  virtual void config(const std::map<std::string, std::string> &params) override;
  virtual auto clone() -> Module* override;
  virtual void pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) override;

private:
  class Target : public Pooled<Target> {
  public:
    void open(const std::string &address, std::shared_ptr<Context> ctx = nullptr);
    void input(std::unique_ptr<Object> obj);
    void close();
  private:
    Session *m_session = nullptr;
  };

  class Pool {
  public:
    auto get(const std::string &name) -> std::shared_ptr<Target>;
  private:
    std::map<std::string, std::shared_ptr<Target>> m_targets;
  };

  std::string m_to;
  std::string m_session_name;
  std::shared_ptr<Pool> m_pool;
  std::shared_ptr<Target> m_target;
  std::list<std::unique_ptr<Object>> m_buffer;
  bool m_buffering = false;
};

NS_END

#endif // CLONE_HPP
