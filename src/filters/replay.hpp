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

#ifndef REPLAY_HPP
#define REPLAY_HPP

#include "filter.hpp"
#include "input.hpp"
#include "timer.hpp"
#include "options.hpp"

namespace pipy {

//
// Replay
//

class Replay : public Filter, public EventSource, public InputSource {
public:
  struct Options : public pipy::Options {
    double delay = 0;
    pjs::Ref<pjs::Function> delay_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Replay(const Options &options);

private:
  Replay(const Replay &r);
  ~Replay();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void on_tap_open() override;
  virtual void on_tap_close() override;
  virtual void dump(Dump &d) override;

  Options m_options;
  pjs::Ref<Pipeline> m_pipeline;
  EventBuffer m_buffer;
  Timer m_timer;
  bool m_replay_scheduled = false;
  bool m_paused = false;
  bool m_shutdown = false;

  void schedule_replay();
  void replay();
};

} // namespace pipy

#endif // REPLAY_HPP
