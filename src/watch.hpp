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

#ifndef WATCH_HPP
#define WATCH_HPP

#include "pjs/pjs.hpp"
#include "event.hpp"
#include "pipeline.hpp"
#include "codebase.hpp"
#include "net.hpp"

#include <set>

namespace pipy {

class PipelineLayout;

class Watch : public EventTarget {
public:
  static auto make(const std::string &filename, PipelineLayout *layout) -> Watch* {
    return new Watch(filename, layout);
  }

  auto filename() const -> pjs::Str* { return m_filename; }
  auto pipeline_layout() const -> PipelineLayout* { return m_pipeline_layout; }
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  bool active() const;
  void start();
  void end();

private:
  Watch(const std::string &filename, PipelineLayout *layout);
  ~Watch();

  pjs::Ref<pjs::Str> m_filename;
  pjs::Ref<Codebase::Watch> m_watch;
  pjs::Ref<PipelineLayout> m_pipeline_layout;
  pjs::Ref<Pipeline> m_pipeline;
  Net& m_net;

  void on_update(const std::list<std::string> &filename);

  virtual void on_event(Event *evt) override;
};

} // namespace pipy

#endif // WATCH_HPP
