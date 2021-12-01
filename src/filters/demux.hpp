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

namespace pipy {

//
// QueueDemuxer
//

class QueueDemuxer : public EventFunction {
public:
  void reset();
  void isolate();

protected:
  virtual auto on_new_sub_pipeline() -> Pipeline* = 0;

private:
  class Stream;

  List<Stream> m_streams;
  bool m_isolated = false;

  void on_event(Event *evt) override;
  void flush();

  //
  // QueueDemuxer::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public List<Stream>::Item,
    public EventTarget
  {
    Stream(QueueDemuxer *demux);
    ~Stream();

    void data(Data *data);
    void end(MessageEnd *end);

    QueueDemuxer* m_demuxer;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<MessageStart> m_start;
    Data m_buffer;
    bool m_input_end = false;
    bool m_output_end = false;
    bool m_isolated = false;

    virtual void on_event(Event *evt) override;

    friend class QueueDemuxer;
  };

  friend class Stream;
};

//
// Demux
//

class Demux : public Filter, public QueueDemuxer {
public:
  Demux();

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  virtual auto on_new_sub_pipeline() -> Pipeline* override;
};

} // namespace pipy

#endif // DEMUX_HPP
