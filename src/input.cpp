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
#include "pipeline.hpp"
#include "logging.hpp"

namespace pipy {

//
// FlushTarget
//

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

InputContext* InputContext::s_stack = nullptr;

InputContext::InputContext(InputSource *source)
  : m_tap(source ? source->tap() : new InputSource::Tap())
{
  m_origin = s_stack ? s_stack : this;
  m_next = s_stack;
  s_stack = this;
}

InputContext::~InputContext() {

  // Flush all pumping targets
  while (auto *target = m_flush_targets_pumping.head()) {
    m_flush_targets_pumping.remove(target);
    target->m_origin = nullptr;
    target->on_flush();
  }

  // Flush all terminating targets
  while (auto *target = m_flush_targets_terminating.head()) {
    m_flush_targets_terminating.remove(target);
    target->m_origin = nullptr;
    target->on_flush();
  }

  // Clean up pipelines
  m_cleaning_up = true;
  for (
    auto *p = m_pipelines;
    p; p = p->m_next_auto_release
  ) {
    p->m_auto_release = false;
    p->release();
  }

  s_stack = m_next;
}

void InputContext::add(Pipeline *pipeline) {
  pipeline->retain();
  if (s_stack) {
    if (s_stack->m_cleaning_up) {
      Log::error(
        "[pipeline %p] auto-release recursion, name = %s",
        pipeline, pipeline->def()->name()->c_str()
      );
    }
    pipeline->m_auto_release = true;
    pipeline->m_next_auto_release = s_stack->m_pipelines;
    s_stack->m_pipelines = pipeline;
  } else {
    pipeline->release();
  }
}

} // namespace pipy
