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

#ifndef COUNTER_HPP
#define COUNTER_HPP

#include "module.hpp"
#include "object.hpp"

#include <chrono>
#include <map>
#include <string>
#include <vector>

NS_BEGIN

//
// Counter
//

class Counter : public Module {
public:
  Counter();

private:
  ~Counter();

  virtual auto help() -> std::list<std::string> override;
  virtual void config(const std::map<std::string, std::string> &params) override;
  virtual auto clone() -> Module* override;
  virtual void pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) override;

  Object::Type m_when;
  std::string m_label;
  std::string m_latency_since;
  std::shared_ptr<std::vector<int>> m_latency_buckets;
};

NS_END

#endif // COUNTER_HPP
