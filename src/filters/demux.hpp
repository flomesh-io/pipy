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

#ifndef DEMUX_HPP
#define DEMUX_HPP

#include "filter.hpp"
#include "data.hpp"
#include "list.hpp"
#include "pipeline.hpp"
#include "options.hpp"

namespace pipy {

//
// Demux
//

class Demux : public Filter {
public:
  struct Options : public pipy::Options {
    pjs::Ref<pjs::Function> message_key_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Demux(const Options &options);

private:
  Demux(const Demux &r);
  ~Demux();

  //
  // Demux::Request
  //

  class Request :
    public pjs::Pooled<Request>,
    public List<Request>::Item,
    public EventTarget
  {
  public:
    Request(Demux *demux, Pipeline *pipeline);

    void input(Event *evt);

  private:
  public:
    virtual void on_event(Event *evt) override;

    Demux* m_demux;
    pjs::Ref<Pipeline> m_pipeline;
    EventBuffer m_buffer;
    bool m_started = false;
    bool m_ended = false;
  };

  Options m_options;
  List<Request> m_requests;
  bool m_started = false;
  bool m_has_shutdown = false;

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  void clear_requests();
};

} // namespace pipy

#endif // DEMUX_HPP
