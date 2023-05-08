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

#include "watch.hpp"

namespace pipy {

Watch::Watch(const std::string &filename, PipelineLayout *layout)
  : m_filename(pjs::Str::make(filename))
  , m_pipeline_layout(layout)
  , m_net(Net::current())
{
}

Watch::~Watch() {
  if (m_watch) {
    m_watch->close();
  }
}

bool Watch::active() const {
  return m_pipeline;
}

void Watch::start() {
  m_watch = Codebase::current()->watch(
    m_filename->str(),
    [this]() { on_update(); }
  );
}

void Watch::end() {
  delete this;
}

void Watch::on_update() {
  m_net.post(
    [this]() {
      InputContext ic;
      if (!active()) {
        m_pipeline = Pipeline::make(
          m_pipeline_layout,
          m_pipeline_layout->new_context()
        );
        m_pipeline->chain(EventTarget::input());
        m_pipeline->start();
      }
    }
  );
}

void Watch::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_pipeline = nullptr;
  }
}

} // namespace pipy
