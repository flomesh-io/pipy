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
// DemuxSession
//

class DemuxSession : public EventFunction {
protected:
  virtual auto on_demux_open_stream() -> EventFunction* = 0;
  virtual void on_demux_close_stream(EventFunction *stream) = 0;
  virtual void on_demux_complete() = 0;
};

//
// DemuxQueue
//

class DemuxQueue : public DemuxSession {
public:
  void reset();
  auto stream_count() const -> int { return m_stream_count; }
  void output_count(int output_count) { m_output_count = output_count; }
  void wait_output() { m_waiting_output_requested = true; }
  void increase_output_count(int n) { if (auto r = m_receivers.head()) r->increase_output_count(n); }
  void dedicate() { m_dedication_requested = true; }

protected:
  virtual void on_demux_queue_dedicate(EventFunction *stream) {}

private:

  //
  // DemuxQueue::Stream
  //

  struct Stream : public pjs::Pooled<Stream> {
    EventFunction* handler;
    bool end_input = false;
    bool end_output = false;
  };

  auto open_stream() -> Stream* {
    auto s = new Stream;
    s->handler = on_demux_open_stream();
    m_stream_count++;
    return s;
  }

  void close_stream_input(Stream *s) {
    if (s) {
      if (!s->end_input) {
        s->end_input = true;
        recycle_stream(s);
      }
    }
  }

  void close_stream_output(Stream *s) {
    if (s) {
      if (!s->end_output) {
        s->end_output = true;
        recycle_stream(s);
      }
    }
  }

  void close_stream(Stream *s) {
    if (s) {
      auto h = s->handler;
      on_demux_close_stream(h);
      delete s;
    }
  }

  void recycle_stream(Stream *s) {
    if (s->end_input && s->end_output) {
      close_stream(s);
      if (!--m_stream_count) on_demux_complete();
    }
  }

  //
  // DemuxQueue::Receiver
  //

  class Receiver :
    public pjs::Pooled<Receiver>,
    public List<Receiver>::Item,
    public EventTarget
  {
  public:
    Receiver(DemuxQueue *queue, Stream *stream, int output_count)
      : m_queue(queue)
      , m_stream(stream)
      , m_output_count(output_count) {}

    auto stream() const -> Stream* { return m_stream; }
    void increase_output_count(int n) { m_output_count += n; }
    bool flush();

  private:
    virtual void on_event(Event *evt) override;

    DemuxQueue* m_queue;
    Stream* m_stream;
    MessageReader m_reader;
    MessageBuffer m_buffer;
    int m_output_count;
    bool m_has_message_started = false;
  };

  //
  // DemuxQueue::Waiter
  //

  class Waiter :
    public pjs::Pooled<Waiter>,
    public List<Waiter>::Item,
    public EventTarget
  {
  public:
    Waiter(DemuxQueue *queue, Stream *stream)
      : m_queue(queue)
      , m_stream(stream) {}

    auto stream() const -> Stream* { return m_stream; }

  private:
    virtual void on_event(Event *evt) override;

    DemuxQueue* m_queue;
    Stream* m_stream;
  };

  virtual void on_event(Event *evt) override;

  void queue_event(Event *evt);
  void start_waiting_output();
  void continue_input();
  bool check_dedicated();
  void shift_receiver();
  void clear_receivers(bool reset);
  void clear_waiters(bool reset);

  Stream* m_input_stream = nullptr;
  EventBuffer m_buffer;
  List<Receiver> m_receivers;
  List<Waiter> m_waiters;
  pjs::Ref<InputSource::Tap> m_closed_tap;
  int m_stream_count = 0;
  int m_output_count = 1;
  bool m_waiting_output_requested = false;
  bool m_waiting_output = false;
  bool m_dedication_requested = false;
  bool m_dedicated = false;
};

//
// Demux
//

class Demux : public Filter, public DemuxQueue {
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
  virtual void dump(Dump &d) override;

  virtual auto on_demux_open_stream() -> EventFunction* override;
  virtual void on_demux_close_stream(EventFunction *stream) override;
  virtual void on_demux_complete() override;

  Options m_options;
  pjs::Ref<StreamEnd> m_eos;
};

} // namespace pipy

#endif // DEMUX_HPP
