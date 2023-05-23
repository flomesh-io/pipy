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

#ifndef BRANCH_HPP
#define BRANCH_HPP

#include "filter.hpp"
#include "message.hpp"

#include <vector>
#include <memory>
#include <utility>

namespace pipy {

//
// BranchBase
//

class BranchBase : public Filter {
public:
  BranchBase(int count, pjs::Function **conds, const pjs::Value *layouts);

protected:
  BranchBase(const BranchBase &r);
  ~BranchBase();

  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual bool choose(Event *evt) = 0;

  bool find_branch(int argc, pjs::Value *args);

private:
  struct Condition {
    pjs::Ref<pjs::Function> func;
  };

  std::shared_ptr<std::vector<Condition>> m_conditions;
  pjs::Ref<Pipeline> m_pipeline;
  EventBuffer m_buffer;
  bool m_chosen = false;
};

//
// Branch
//

class Branch : public BranchBase {
public:
  using BranchBase::BranchBase;

protected:
  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual bool choose(Event *evt) override;
};

//
// BranchMessageStart
//

class BranchMessageStart : public BranchBase {
public:
  using BranchBase::BranchBase;

protected:
  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
  virtual bool choose(Event *evt) override;
};

//
// BranchMessage
//

class BranchMessage : public BranchBase {
public:
  using BranchBase::BranchBase;

protected:
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void dump(Dump &d) override;
  virtual bool choose(Event *evt) override;

private:
  MessageReader m_reader;
};

} // namespace pipy

#endif // BRANCH_HPP
