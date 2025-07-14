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

#ifndef INPUT_HPP
#define INPUT_HPP

#include "pjs/pjs.hpp"
#include "list.hpp"

namespace pipy {

class AutoReleased;
class ContextGroup;
class InputContext;

//
// InputSource
//

class InputSource {
public:

  //
  // InputSource::Tap
  //

  class Tap :
    public pjs::Pooled<Tap>,
    public pjs::RefCount<Tap>
  {
  public:
    void open() {
      if (m_source) {
        m_source->on_tap_open();
      }
      m_closed = false;
    }

    void close() {
      if (m_source) {
        m_source->on_tap_close();
      }
      m_closed = true;
    }

  private:
    Tap(InputSource *source = nullptr)
      : m_source(source) {}

    void detach() {
      m_source = nullptr;
    }

    InputSource* m_source;
    bool m_closed = false;

    friend class InputSource;
    friend class InputContext;
  };

  //
  // InputSource::Congestion
  //

  class Congestion {
  private:
    struct ClosedTap : public pjs::Pooled<ClosedTap>, public List<ClosedTap>::Item {
      pjs::Ref<InputSource::Tap> tap;
      ClosedTap(InputSource::Tap *t) : tap(t) {}
    };

    List<ClosedTap> m_closed_taps;

  public:
    ~Congestion() { end(); }

    void begin();
    void end();
  };

protected:
  ~InputSource() {
    if (m_tap) {
      m_tap->detach();
    }
  }

  auto tap() -> Tap* {
    if (!m_tap) {
      m_tap = new Tap(this);
    }
    return m_tap;
  }

  virtual void on_tap_open() = 0;
  virtual void on_tap_close() = 0;

private:
  pjs::Ref<Tap> m_tap;

  friend class InputContext;
};

//
// FlushTarget
//

class FlushTarget : public List<FlushTarget>::Item {
protected:
  FlushTarget(bool is_terminating = false)
    : m_is_terminating(is_terminating) {}

  ~FlushTarget();

  void need_flush();

private:
  virtual void on_flush() = 0;

  InputContext* m_origin = nullptr;
  bool m_is_terminating;

  friend class InputContext;
};

//
// InputContext
//

class InputContext
{
public:
  InputContext(InputSource *source = nullptr);
  ~InputContext();

  static auto origin() -> InputContext* {
    return s_stack ? s_stack->m_origin : nullptr;
  }

  static auto tap() -> InputSource::Tap* {
    return s_stack ? s_stack->m_tap.get() : nullptr;
  }

private:
  InputContext* m_origin;
  InputContext* m_next;
  List<ContextGroup> m_context_groups;
  List<FlushTarget> m_flush_targets_pumping;
  List<FlushTarget> m_flush_targets_terminating;
  pjs::Ref<InputSource::Tap> m_tap;
  AutoReleased* m_auto_released = nullptr;

  thread_local static InputContext* s_stack;

  static void auto_release(AutoReleased *obj);

  friend class FlushTarget;
  friend class AutoReleased;
  friend class ContextGroup;
};

//
// AutoReleased
//

class AutoReleased : public pjs::RefCount<AutoReleased> {
protected:
  void reset() {
    m_auto_release = false;
  }

  void auto_release() {
    if (!m_auto_release) {
      InputContext::auto_release(this);
    }
  }

private:
  virtual void on_auto_release() = 0;

  AutoReleased* m_next_auto_release = nullptr;
  bool m_auto_release = false;

  void finalize() {
    on_auto_release();
  }

  friend class pjs::RefCount<AutoReleased>;
  friend class InputContext;
};

} // namespace pipy

#endif // INPUT_HPP
