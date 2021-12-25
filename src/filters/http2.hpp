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

#ifndef HTTP2_HPP
#define HTTP2_HPP

#include "api/http.hpp"
#include "data.hpp"
#include "pipeline.hpp"
#include "list.hpp"
#include "mux.hpp"

#include <map>
#include <vector>

namespace pipy {
namespace http2 {

//
// ErrorCode
//

enum ErrorCode {
  NO_ERROR            = 0x0,
  PROTOCOL_ERROR      = 0x1,
  INTERNAL_ERROR      = 0x2,
  FLOW_CONTROL_ERROR  = 0x3,
  SETTINGS_TIMEOUT    = 0x4,
  STREAM_CLOSED       = 0x5,
  FRAME_SIZE_ERROR    = 0x6,
  REFUSED_STREAM      = 0x7,
  CANCEL              = 0x8,
  COMPRESSION_ERROR   = 0x9,
  CONNECT_ERROR       = 0xa,
  ENHANCE_YOUR_CALM   = 0xb,
  INADEQUATE_SECURITY = 0xc,
  HTTP_1_1_REQUIRED   = 0xd,
};

//
// Frame
//

struct Frame {
  enum Type {
    DATA          = 0x0,
    HEADERS       = 0x1,
    PRIORITY      = 0x2,
    RST_STREAM    = 0x3,
    SETTINGS      = 0x4,
    PUSH_PROMISE  = 0x5,
    PING          = 0x6,
    GOAWAY        = 0x7,
    WINDOW_UPDATE = 0x8,
    CONTINUATION  = 0x9,
  };

  enum Flags {
    BIT_ACK         = 0x01,
    BIT_END_STREAM  = 0x01,
    BIT_END_HEADERS = 0x04,
    BIT_PADDED      = 0x08,
    BIT_PRIORITY    = 0x20,
  };

  int stream_id;
  uint8_t type;
  uint8_t flags;
  Data payload;

  bool is_ACK() const { return flags & BIT_ACK; }
  bool is_END_STREAM() const { return flags & BIT_END_STREAM; }
  bool is_END_HEADERS() const { return flags & BIT_END_HEADERS; }
  bool is_PADDED() const { return flags & BIT_PADDED; }
  bool is_PRIORITY() const { return flags & BIT_PRIORITY; }
};

//
// FrameDecoder
//

class FrameDecoder {
protected:
  void reset();
  void deframe(Data *data);

  virtual void on_deframe(Frame &frame) = 0;
  virtual void on_deframe_error() = 0;

private:
  enum State {
    STATE_HEADER,
    STATE_PAYLOAD,
  };

  State m_state = STATE_HEADER;
  uint8_t m_header_buf[9];
  int m_header_ptr = 0;
  int m_payload_size = 0;
  Frame m_frame;
};

//
// FrameEncoder
//

class FrameEncoder {
protected:
  void frame(Frame &frm, EventTarget::Input *out);
  void RST_STREAM(int id, ErrorCode err, EventTarget::Input *out);
  void GOAWAY(int id, ErrorCode err, EventTarget::Input *out);

private:
  void header(uint8_t *buf, int id, uint8_t type, uint8_t flags, size_t size);
};

//
// HeaderDecoder
//

class HeaderDecoder {
public:
  HeaderDecoder();

  void start(bool is_response);
  bool started() const { return m_head; }
  bool decode(Data &data);
  void end(pjs::Ref<http::MessageHead> &head);

private:
  static const int TABLE_SIZE = 256;

  enum State {
    ERROR,

    INDEX_PREFIX,
    INDEX_OCTETS,

    NAME_PREFIX,
    NAME_LENGTH,
    NAME_STRING,

    VALUE_PREFIX,
    VALUE_LENGTH,
    VALUE_STRING,
  };

  struct Entry {
    pjs::Ref<pjs::Str> name;
    pjs::Ref<pjs::Str> value;
  };

  struct Huffman {
    uint16_t left = 0;
    union {
      uint16_t right = 0;
      uint16_t code;
    };
  };

  State m_state;
  bool m_is_response;
  bool m_is_new;
  uint8_t m_prefix;
  uint8_t m_exp;
  uint32_t m_int;
  uint16_t m_ptr;
  Data m_buffer;
  pjs::Ref<http::MessageHead> m_head;
  pjs::Ref<pjs::Str> m_name;
  pjs::PooledArray<Entry>* m_table;
  size_t m_table_head = 0;
  size_t m_table_tail = 0;

  bool read_int(uint8_t c);
  bool read_str(uint8_t c);
  void index_prefix(uint8_t prefix);
  void index_end();
  void name_prefix(uint8_t prefix);
  void value_prefix(uint8_t prefix);

  void add_field(pjs::Str *name, pjs::Str *value);
  auto get_entry(size_t i) -> const Entry*;
  void new_entry(pjs::Str *name, pjs::Str *value);

  //
  // HeaderDecoder::StaticTable
  //

  class StaticTable {
  public:
    StaticTable();
    auto get() -> const std::vector<Entry>& { return m_table; }
  private:
    std::vector<Entry> m_table;
  };

  //
  // HeaderDecoder::HuffmanTree
  //

  class HuffmanTree {
  public:
    HuffmanTree();
    auto get() -> const std::vector<Huffman>& { return m_tree; }
  private:
    std::vector<Huffman> m_tree;
  };

  static StaticTable s_static_table;
  static HuffmanTree s_huffman_tree;
};

//
// HeaderEncoder
//

class HeaderEncoder {
public:
  void encode(
    bool is_response,
    pjs::Object *head,
    Data &data
  );

private:
  void encode_header_field(
    Data &data,
    pjs::Str *k,
    pjs::Str *v
  );

  void encode_int(Data &data, uint8_t prefix, int prefix_len, uint32_t n);
  void encode_str(Data &data, pjs::Str *s);

  struct Entry {
    int index = 0;
    std::map<pjs::Ref<pjs::Str>, int> values;
  };

  //
  // HeaderEncoder::StaticTable
  //

  class StaticTable {
  public:
    StaticTable();
    auto find(pjs::Str *name) -> const Entry*;
  private:
    std::map<pjs::Ref<pjs::Str>, Entry> m_table;
  };

  static StaticTable m_static_table;
};

//
// StreamBase
//

class StreamBase {
protected:
  enum State {
    IDLE,
    RESERVED_LOCAL,
    RESERVED_REMOTE,
    OPEN,
    HALF_CLOSED_LOCAL,
    HALF_CLOSED_REMOTE,
    CLOSED,
  };

  StreamBase(
    int id,
    bool is_server_side,
    HeaderDecoder &header_decoder,
    HeaderEncoder &header_encoder
  );

  auto id() const -> int { return m_id; }

  void on_frame(Frame &frm);
  void on_event(Event *evt);

  virtual void frame(Frame &frm) = 0;
  virtual void event(Event *evt) = 0;
  virtual void flush() = 0;
  virtual void close() = 0;
  virtual void stream_error(ErrorCode err) = 0;
  virtual void connection_error(ErrorCode err) = 0;

private:
  int m_id;
  bool m_is_server_side;
  bool m_is_reserved = false;
  bool m_is_tunnel = false;
  bool m_end_stream = false;
  State m_state = IDLE;
  HeaderDecoder& m_header_decoder;
  HeaderEncoder& m_header_encoder;

  bool parse_padding(Frame &frm);
  bool parse_priority(Frame &frm);
  void parse_headers(Frame &frm);
  void stream_end();
};

//
// Demuxer
//

class Demuxer :
  public EventFunction,
  public FrameDecoder,
  public FrameEncoder
{
public:
  virtual ~Demuxer();

  auto initial_stream() -> Input*;
  void shutdown();

protected:
  virtual auto on_new_sub_pipeline() -> Pipeline* = 0;

private:
  class Stream;
  class InitialStream;

  std::map<int, Stream*> m_streams;
  HeaderDecoder m_header_decoder;
  HeaderEncoder m_header_encoder;
  int m_last_received_stream_id = 0;
  bool m_has_sent_preface = false;
  bool m_has_gone_away = false;

  void stream_close(int id);
  void stream_error(int id, ErrorCode err);
  void connection_error(ErrorCode err);

  // client -> session
  void on_event(Event *evt) override;
  void on_deframe(Frame &frm) override;
  void on_deframe_error() override;

  // client <- session
  void frame(Frame &frm);
  void flush() { output(Data::flush()); }

  //
  // Demuxer::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public StreamBase,
    public EventSource
  {
    Stream(Demuxer *demuxer, int id);

    Demuxer* m_demuxer;
    pjs::Ref<Pipeline> m_pipeline;

    // session -> stream
    void on_frame(Frame &frm) { StreamBase::on_frame(frm); }

    // stream -> server
    void event(Event *evt) override { EventSource::output(evt); }

    // stream <- server
    void on_event(Event *evt) override  { StreamBase::on_event(evt); }

    // session <- stream
    void frame(Frame &frm) override { m_demuxer->frame(frm); }
    void flush() override { m_demuxer->flush(); }

    // close
    void close() override { m_demuxer->stream_close(id()); }

    // errors
    void stream_error(ErrorCode err) override { m_demuxer->stream_error(id(), err); }
    void connection_error(ErrorCode err) override { m_demuxer->connection_error(err); }

    friend class Demuxer;
    friend class InitialStream;
  };

  //
  // Demuxer::InitialStream
  //

  struct InitialStream : public EventTarget {
    Stream* stream = nullptr;
    friend class Demuxer;
    virtual void on_event(Event *evt) override {
      if (stream) stream->event(evt);
    }
  };

  InitialStream m_initial_stream;
};

//
// Muxer
//

class Muxer :
  public EventSource,
  public FrameDecoder,
  public FrameEncoder
{
public:
  void open(EventFunction *session);
  auto stream() -> EventFunction*;
  void close(EventFunction *stream);
  void close();

private:
  class Stream;

  std::map<int, Stream*> m_streams;
  HeaderDecoder m_header_decoder;
  HeaderEncoder m_header_encoder;
  int m_last_sent_stream_id = -1;
  bool m_has_sent_preface = false;
  bool m_has_gone_away = false;

  void stream_close(int id);
  void stream_error(int id, ErrorCode err);
  void connection_error(ErrorCode err);

  // session -> server
  void frame(Frame &frm);
  void flush() { EventSource::output(Data::flush()); }

  // session <- server
  void on_event(Event *evt) override;
  void on_deframe(Frame &frm) override;
  void on_deframe_error() override;

  //
  // Muxer::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public StreamBase,
    public EventFunction
  {
    Stream(Muxer *muxer, int id);

    Muxer* m_muxer;

    // client -> stream
    void on_event(Event *evt) override { StreamBase::on_event(evt); }

    // stream -> session
    void frame(Frame &frm) override { m_muxer->frame(frm); }
    void flush() override { m_muxer->flush(); }

    // stream <- session
    void on_frame(Frame &frm) { StreamBase::on_frame(frm); }

    // client <- stream
    void event(Event *evt) override { EventFunction::output(evt); }

    // close
    void close() override { m_muxer->stream_close(id()); }

    // errors
    void stream_error(ErrorCode err) override { m_muxer->stream_error(id(), err); }
    void connection_error(ErrorCode err) override { m_muxer->connection_error(err); }

    friend class Muxer;
  };
};

} // namespace http2
} // namespace pipy

#endif // HTTP2_HPP
