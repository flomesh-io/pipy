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

#include "fork.hpp"
#include "pipeline.hpp"
#include "context.hpp"
#include "log.hpp"

namespace pipy {

//
// Fork
//

Fork::Fork() : m_mode(FORK)
{
}

Fork::Fork(const pjs::Value &init_arg)
  : m_mode(FORK)
  , m_init_arg(init_arg)
{
}

Fork::Fork(Mode mode, const pjs::Value &init_arg)
  : m_mode(mode)
  , m_init_arg(init_arg)
{
}

Fork::Fork(const Fork &r)
  : Filter(r)
  , m_mode(r.m_mode)
  , m_init_arg(r.m_init_arg)
{
}

Fork::~Fork() {
}

void Fork::dump(Dump &d) {
  Filter::dump(d);
  switch (m_mode) {
    case JOIN:
      d.name = "forkJoin";
      d.out_type = Dump::OUTPUT_FROM_SUBS;
      break;
    case RACE:
      d.name = "forkRace";
      d.out_type = Dump::OUTPUT_FROM_SUBS;
      break;
    default:
      d.name = "fork";
      d.out_type = Dump::OUTPUT_FROM_SELF;
      break;
  }
}

auto Fork::clone() -> Filter* {
  return new Fork(*this);
}

void Fork::reset() {
  Filter::reset();
  if (m_branches) {
    m_branches->free();
    m_branches = nullptr;
  }
  m_winner = nullptr;
  m_buffer.clear();
  m_counter = 0;
  m_waiting = false;
}

void Fork::process(Event *evt) {
  if (!m_branches) {
    pjs::Value init_arg;
    if (!eval(m_init_arg, init_arg)) return;
    if (init_arg.is_array()) {
      auto arr = init_arg.as<pjs::Array>();
      auto len = arr->length();
      m_branches = pjs::PooledArray<Branch>::make(len);
      if (m_mode == JOIN && len > 0) m_waiting = true;
      for (int i = 0; i < len; i++) {
        auto pipeline = sub_pipeline(0, true);
        auto &branch = m_branches->at(i);
        branch.fork = this;
        branch.pipeline = pipeline;
        pjs::Value args[2];
        arr->get(i, args[0]);
        args[1].set(i);
        pipeline->chain(branch.input());
        pipeline->start(2, args);
      }
    } else {
      m_branches = pjs::PooledArray<Branch>::make(1);
      if (m_mode == JOIN) m_waiting = true;
      auto pipeline = sub_pipeline(0, m_mode != FORK)->start(1, &init_arg);
      auto &branch = m_branches->at(0);
      branch.fork = this;
      branch.pipeline = pipeline;
      pipeline->chain(branch.input());
      pipeline->start(1, &init_arg);
    }
  }

  if (m_branches) {
    for (int i = 0; i < m_branches->size(); i++) {
      m_branches->at(i).pipeline->input()->input(evt->clone());
    }
  }

  if (m_waiting) {
    m_buffer.push(evt);
  } else if (m_mode != RACE) {
    Filter::output(evt);
  }
}

void Fork::on_branch_output(Branch *branch, Event *evt) {
  if (m_mode == JOIN) {
    if (evt->is<StreamEnd>()) {
      m_counter++;
      if (m_counter >= m_branches->size()) {
        m_waiting = false;
        m_buffer.flush(
          [this](Event *evt) {
            Filter::output(evt);
          }
        );
      }
    }
  } else if (m_mode == RACE) {
    if (!m_winner) {
      m_winner = branch;
    }
    if (branch == m_winner) {
      Filter::output(evt);
    }
  } else {
    if (evt->is<StreamEnd>()) {
      Filter::output(evt);
    }
  }
}

void Fork::Branch::on_event(Event *evt) {
  fork->on_branch_output(this, evt);
}

} // namespace pipy
