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

#include "input.hpp"
#include "context.hpp"

namespace pipy {

//
// FlushTarget
//

FlushTarget::~FlushTarget() {
  if (m_origin) {
    if (m_is_terminating) {
      m_origin->m_flush_targets_terminating.remove(this);
    } else {
      m_origin->m_flush_targets_pumping.remove(this);
    }
  }
}

void FlushTarget::need_flush() {
  if (!m_origin) {
    auto *origin = InputContext::origin();
    if (m_is_terminating) {
      origin->m_flush_targets_terminating.push(this);
    } else {
      origin->m_flush_targets_pumping.push(this);
    }
    m_origin = origin;
  }
}

//
// InputContext
//

thread_local InputContext* InputContext::s_stack = nullptr;

InputContext::InputContext(InputSource *source)
  : m_tap(source ? source->tap() : new InputSource::Tap())
{
  m_origin = s_stack ? s_stack->m_origin : this;
  m_next = s_stack;
  s_stack = this;
}

InputContext::~InputContext() {

  // Notify context groups
  for (auto *g = m_context_groups.head(); g; g = g->next()) g->notify();
  while (auto *g = m_context_groups.head()) {
    g->m_input_context = nullptr;
    m_context_groups.remove(g);
  }

  if (m_origin == this) {

    // Run micro-tasks
    int max_runs = 100;
    while (max_runs > 0 && pjs::Promise::run()) max_runs--;

    // Flush all pumping targets
    while (auto *target = m_flush_targets_pumping.head()) {
      m_flush_targets_pumping.remove(target);
      target->on_flush();
      target->m_origin = nullptr;
    }

    // Clean up pipelines
    auto *p = m_auto_released;
    m_auto_released = nullptr;
    while (p) {
      auto *obj = p; p = p->m_next_auto_release;
      obj->m_auto_release = false;
      obj->release();
    }

    // Flush all terminating targets
    while (auto *target = m_flush_targets_terminating.head()) {
      m_flush_targets_terminating.remove(target);
      target->on_flush();
      target->m_origin = nullptr;
    }

  }

  s_stack = m_next;
}

void InputContext::auto_release(AutoReleased *obj) {
  if (!obj->m_auto_release) {
    auto ctx = s_stack->m_origin;
    obj->retain();
    obj->m_auto_release = true;
    obj->m_next_auto_release = ctx->m_auto_released;
    ctx->m_auto_released = obj;
  }
}

void InputContext::defer_notify(ContextGroup *grp) {
  auto *ic = s_stack;
  ic->m_context_groups.push(grp);
  grp->m_input_context = ic;
}

} // namespace pipy
