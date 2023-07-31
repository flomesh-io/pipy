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

#include "branch.hpp"
#include "pipeline.hpp"

namespace pipy {

//
// BranchBase
//

BranchBase::BranchBase(int count, pjs::Function **conds, const pjs::Value *layouts)
  : m_conditions(std::make_shared<std::vector<Condition>>())
  , m_buffer(Filter::buffer_stats())
{
  m_conditions->resize(count);
  for (int i = 0; i < count; i++) {
    m_conditions->at(i).func = conds[i];
    add_sub_pipeline(layouts[i]);
  }
}

BranchBase::BranchBase(const BranchBase &r)
  : Filter(r)
  , m_conditions(r.m_conditions)
{
}

BranchBase::~BranchBase()
{
}

void BranchBase::reset() {
  Filter::reset();
  m_buffer.clear();
  m_pipeline = nullptr;
  m_chosen = false;
}

void BranchBase::chain() {
  Filter::chain();
  if (m_pipeline) {
    m_pipeline->chain(Filter::output());
  }
}

void BranchBase::process(Event *evt) {
  if (!m_chosen) {
    if (!choose(evt)) return;
  }

  if (!m_chosen) {
    m_buffer.push(evt);
  } else if (auto *p = m_pipeline.get()) {
    output(evt, p->input());
  } else {
    output(evt);
  }
}

bool BranchBase::find_branch(int argc, pjs::Value *args) {
  const auto &conditions = *m_conditions;
  for (int i = 0; i < conditions.size(); i++) {
    const auto &cond = conditions[i];
    if (cond.func) {
      pjs::Value ret;
      if (!callback(cond.func, argc, args, ret)) return false;
      m_chosen = ret.to_boolean();
    } else {
      m_chosen = true;
    }
    if (m_chosen) {
      if (auto *pipeline = sub_pipeline(i, false, Filter::output())) {
        pipeline->start();
        m_pipeline = pipeline;
        m_buffer.flush([&](Event *evt) {
          output(evt, pipeline->input());
        });
      } else {
        m_buffer.flush([&](Event *evt) {
          output(evt);
        });
      }
      break;
    }
  }
  return true;
}

//
// Branch
//

void Branch::dump(Dump &d) {
  Filter::dump(d);
  d.name = "branch";
}

auto Branch::clone() -> Filter* {
  return new Branch(*this);
}

bool Branch::choose(Event *evt) {
  return find_branch(0, nullptr);
}

//
// BranchMessageStart
//

void BranchMessageStart::dump(Dump &d) {
  Filter::dump(d);
  d.name = "branchMessageStart";
}

auto BranchMessageStart::clone() -> Filter* {
  return new BranchMessageStart(*this);
}

bool BranchMessageStart::choose(Event *evt) {
  if (evt->is<MessageStart>()) {
    pjs::Value arg(evt);
    return find_branch(1, &arg);
  } else {
    return true;
  }
}

//
// BranchMessage
//

void BranchMessage::dump(Dump &d) {
  Filter::dump(d);
  d.name = "branchMessage";
}

auto BranchMessage::clone() -> Filter* {
  return new BranchMessage(*this);
}

void BranchMessage::reset() {
  BranchBase::reset();
  m_reader.reset();
}

bool BranchMessage::choose(Event *evt) {
  if (auto *msg = m_reader.read(evt)) {
    pjs::Value arg(msg);
    msg->release();
    return find_branch(1, &arg);
  } else {
    return true;
  }
}

} // namespace pipy
