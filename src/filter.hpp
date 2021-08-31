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

#ifndef FILTER_HPP
#define FILTER_HPP

#include "event.hpp"

#include <list>
#include <ostream>

namespace pipy {

class Context;
class Pipeline;
class ReusableSession;

//
// Filter
//

class Filter {
public:
  virtual ~Filter() {}
  virtual auto help() -> std::list<std::string> { return std::list<std::string>(); }
  virtual void dump(std::ostream &out) = 0;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string;
  virtual void bind() {}
  virtual auto clone() -> Filter* = 0;
  virtual void reset() = 0;
  virtual void process(Context *ctx, Event *inp) = 0;

  auto pipeline() const -> Pipeline* { return m_pipeline; }

protected:
  auto pipeline(pjs::Str *name) -> Pipeline*;
  auto new_context(Context *base = nullptr) -> Context*;
  auto out() const -> const Event::Receiver& { return m_output; }
  void output(Event *inp) { m_output(inp); }
  bool output(const pjs::Value &evt);
  bool eval(Context &ctx, pjs::Value &param, pjs::Value &result);
  bool callback(Context &ctx, pjs::Function *func, int argc, pjs::Value argv[], pjs::Value &result);
  void abort();

private:
  Pipeline* m_pipeline = nullptr;
  Filter* m_next = nullptr;
  ReusableSession* m_reusable_session = nullptr;
  Event::Receiver m_output;

  friend class ReusableSession;
  friend class Pipeline;
};

} // namespace pipy

#endif // FILTER_HPP