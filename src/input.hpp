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

namespace pipy {

class Pipeline;
class InputContext;

//
// InputSource
//

class InputSource {
public:
  class Tap :
    public pjs::RefCount<Tap>,
    public pjs::Pooled<Tap>
  {
  public:
    void open() {
      if (m_source) {
        m_source->on_tap_open();
      }
    }

    void close() {
      if (m_source) {
        m_source->on_tap_close();
      }
    }

  private:
    Tap(InputSource *source = nullptr)
      : m_source(source) {}

    void detach() {
      m_source = nullptr;
    }

    InputSource* m_source;

    friend class InputSource;
    friend class InputContext;
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
// InputContext
//

class InputContext
{
public:
  InputContext(InputSource *source = nullptr);
  ~InputContext();

  static auto tap() -> InputSource::Tap* {
    return s_stack ? s_stack->m_tap.get() : nullptr;
  }

private:
  InputContext* m_next;
  pjs::Ref<InputSource::Tap> m_tap;
  Pipeline* m_pipelines = nullptr;
  bool m_cleaning_up = false;

  static InputContext* s_stack;

  static void add(Pipeline *pipeline);

  friend class Pipeline;
};

} // namespace pipy

#endif // INPUT_HPP
