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
#include "message.hpp"
#include "pipeline.hpp"
#include "options.hpp"

namespace pipy {

//
// Demuxer
//

class Demuxer {
private:

  //
  // Demuxer::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public List<Stream>::Item,
    public EventProxy
  {
  public:
    Stream(Demuxer *demuxer) : m_demuxer(demuxer) {
      demuxer->m_streams.push(this);
    }

    ~Stream() {
      m_demuxer->m_streams.remove(this);
    }

    void open(Pipeline *pipeline);
    void end_input();
    void end_output();

  private:
    Demuxer* m_demuxer;
    pjs::Ref<Pipeline> m_pipeline;
    bool m_input_end = false;
    bool m_output_end = false;

    void recycle() {
      if (m_input_end && m_output_end) {
        delete this;
      }
    }

    virtual void on_input(Event *evt) override;
    virtual void on_reply(Event *evt) override;
  };

  List<Stream> m_streams;

protected:
  void reset();
  auto open_stream(Pipeline *pipeline) -> EventFunction*;

public:

  //
  // Demuxer::Queue
  //

  class Queue : public EventFunction {
  public:
    void reset();
    void increase_output_count();
    void dedicate() { m_dedicated_requested = true; }
    void shutdown();

  protected:
    virtual auto on_open_stream() -> EventFunction* = 0;
    virtual auto on_queue_message(MessageStart *start) -> int { return 1; }

  private:
    virtual void on_event(Event *evt) override;

    void queue(Event *evt);
    void wait_output();
    void continue_input();
    bool check_dedicated();
    void shift();
    void clear();
    void close();

    //
    // Demuxer::Queue::Receiver
    //

    class Receiver :
      public pjs::Pooled<Receiver>,
      public List<Receiver>::Item,
      public EventTarget
    {
    public:
      Receiver(Queue *queue, int output_count)
        : m_queue(queue)
        , m_output_count(output_count) {}

      void increase_output_count(int n) { m_output_count += n; }
      bool flush();

    private:
      virtual void on_event(Event *evt) override;

      Queue* m_queue;
      MessageReader m_reader;
      MessageBuffer m_buffer;
      int m_output_count;
      bool m_message_started = false;
    };

    EventFunction* m_stream = nullptr;
    EventBuffer m_buffer;
    List<Receiver> m_receivers;
    pjs::Ref<StreamEnd> m_stream_end;
    pjs::Ref<InputSource::Tap> m_closed_tap;
    bool m_waiting_output_requested = false;
    bool m_waiting_output = false;
    bool m_dedicated_requested = false;
    bool m_dedicated = false;
    bool m_shutdown = false;
    bool m_closed = false;
  };
};

//
// Demux
//

class Demux : public Filter, public Demuxer, public Demuxer::Queue {
public:
  struct Options : public pipy::Options {
    int output_count = 1;
    pjs::Ref<pjs::Function> output_count_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Demux();
  Demux(const Options &options);

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto clone() -> Filter* override;
  virtual void chain() override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void shutdown() override;
  virtual void dump(Dump &d) override;

  virtual auto on_open_stream() -> EventFunction* override;
  virtual auto on_queue_message(MessageStart *start) -> int override;

  Options m_options;
};

} // namespace pipy

#endif // DEMUX_HPP
