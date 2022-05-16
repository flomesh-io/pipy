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
#include "scarce.hpp"
#include "deframer.hpp"

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
// Settings
//

struct Settings {
  enum { MAX_SIZE = 1024 };

  int header_table_size = 0x1000;
  int initial_window_size = 0xffff;
  int max_concurrent_streams = -1;
  int max_frame_size = 0x4000;
  int max_header_list_size = -1;
  bool enable_push = true;

  auto decode(const uint8_t *data, int size) -> ErrorCode;
  auto encode(uint8_t *data) const -> int;
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

  auto decode_window_update(int &increment) -> ErrorCode;
  void encode_window_update(int increment);
};

//
// FrameDecoder
//

class FrameDecoder : public Deframer {
protected:
  FrameDecoder();

  void set_max_frame_size(int n) { m_max_frame_size = n; }
  void deframe(Data *data);

  virtual void on_deframe(Frame &frame) = 0;
  virtual void on_deframe_error(ErrorCode err) = 0;

private:
  enum State {
    STATE_HEADER,
    STATE_PAYLOAD,
  };

  Frame m_frame;
  uint8_t m_header[9];
  pjs::Ref<Data> m_payload;
  int m_max_frame_size = 0x4000;

  virtual auto on_state(int state, int c) -> int override;
};

//
// FrameEncoder
//

class FrameEncoder {
protected:
  void frame(Frame &frm, Data &out);
  void RST_STREAM(int id, ErrorCode err, Data &out);
  void GOAWAY(int id, ErrorCode err, Data &out);

private:
  void header(uint8_t *buf, int id, uint8_t type, uint8_t flags, size_t size);
};

//
// HeaderDecoder
//

class HeaderDecoder {
public:
  HeaderDecoder(const Settings &settings);

  void start(bool is_response, bool is_trailer);
  bool started() const { return m_head; }
  auto decode(Data &data) -> ErrorCode;
  auto end(pjs::Ref<http::MessageHead> &head) -> ErrorCode;

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

  const Settings& m_settings;
  State m_state;
  ErrorCode m_error;
  bool m_is_response;
  bool m_is_trailer;
  bool m_is_new;
  bool m_is_pseudo_end;
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
  bool read_str(uint8_t c, bool lowercase_only);
  void index_prefix(uint8_t prefix);
  void index_end();
  void name_prefix(uint8_t prefix);
  void value_prefix(uint8_t prefix);

  bool add_field(pjs::Str *name, pjs::Str *value);
  auto get_entry(size_t i) -> const Entry*;
  void new_entry(pjs::Str *name, pjs::Str *value);

  void error(ErrorCode err = COMPRESSION_ERROR);

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
    Data::Builder &db,
    pjs::Str *k,
    pjs::Str *v
  );

  void encode_int(Data::Builder &db, uint8_t prefix, int prefix_len, uint32_t n);
  void encode_str(Data::Builder &db, pjs::Str *s, bool lowercase);

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
// Endpoint
//

class Endpoint :
  public FrameDecoder,
  public FrameEncoder
{
public:
  static auto server_stream_count() -> int { return m_server_stream_count; }
  static auto client_stream_count() -> int { return m_client_stream_count; }

private:
  static int m_server_stream_count;
  static int m_client_stream_count;

protected:
  Endpoint(bool is_server_side);
  virtual ~Endpoint();

  class StreamBase;

  virtual void on_output(Event *evt) = 0;
  virtual auto on_new_stream(int id) -> StreamBase* = 0;
  virtual void on_delete_stream(StreamBase *stream) = 0;

private:
  enum {
    INITIAL_SEND_WINDOW_SIZE = 0xffff,
    INITIAL_RECV_WINDOW_SIZE = 0xffff,
  };

  List<StreamBase> m_streams;
  ScarcePointerArray<StreamBase> m_stream_map;
  HeaderDecoder m_header_decoder;
  HeaderEncoder m_header_encoder;
  Settings m_settings;
  Settings m_peer_settings;
  Data m_output_buffer;
  int m_last_received_stream_id = 0;
  int m_send_window = INITIAL_SEND_WINDOW_SIZE;
  int m_recv_window = INITIAL_RECV_WINDOW_SIZE;
  bool m_is_server_side;
  bool m_has_sent_preface = false;
  bool m_has_gone_away = false;
  bool m_processing_frames = false;

protected:
  void upgrade_request(http::RequestHead *head, const Data &body);

  void on_event(Event *evt);
  void on_deframe(Frame &frm) override;
  void on_deframe_error(ErrorCode err) override;
  void frame(Frame &frm);
  void flush();

  auto stream_open(int id) -> StreamBase*;
  void stream_close(int id);
  void stream_error(int id, ErrorCode err);
  void connection_error(ErrorCode err);

  //
  // StreamBase
  //

  class StreamBase :
    public List<StreamBase>::Item
  {
  protected:
    enum {
      INITIAL_SEND_WINDOW_SIZE = 0xffff,
      INITIAL_RECV_WINDOW_SIZE = 0xffff,
    };

    enum State {
      IDLE,
      RESERVED_LOCAL,
      RESERVED_REMOTE,
      OPEN,
      HALF_CLOSED_LOCAL,
      HALF_CLOSED_REMOTE,
      CLOSED,
    };

    StreamBase(Endpoint *endpoint, int id, bool is_server_side);
    virtual ~StreamBase();

    auto id() const -> int { return m_id; }

    bool update_send_window(int delta);
    void on_frame(Frame &frm);
    void on_event(Event *evt);
    void on_pump();

    virtual void event(Event *evt) = 0;

    void frame(Frame &frm) { m_endpoint->frame(frm); }
    void flush() { m_endpoint->flush(); }
    void close() { m_endpoint->stream_close(id()); }
    auto deduct_send(int size) -> int;
    bool deduct_recv(int size);
    void stream_error(ErrorCode err) { m_endpoint->stream_error(id(), err); }
    void connection_error(ErrorCode err) { m_endpoint->connection_error(err); }

  private:
    Endpoint* m_endpoint;
    int m_id;
    bool m_is_server_side;
    bool m_is_tunnel = false;
    bool m_end_headers = false;
    bool m_end_input = false;
    bool m_end_output = false;
    State m_state = IDLE;
    HeaderDecoder& m_header_decoder;
    HeaderEncoder& m_header_encoder;
    Data m_send_buffer;
    int m_send_window = INITIAL_SEND_WINDOW_SIZE;
    int m_recv_window = INITIAL_RECV_WINDOW_SIZE;
    const Settings& m_settings;

    bool parse_padding(Frame &frm);
    bool parse_priority(Frame &frm);
    void parse_headers(Frame &frm);
    bool parse_window_update(Frame &frm);
    void pump();
    void recycle();
    void stream_end();

    friend class Endpoint;
  };
};

//
// Server
//

class Server :
  public Endpoint,
  public EventFunction
{
public:
  Server();
  virtual ~Server();

  auto initial_stream() -> Input*;
  void open();
  void go_away();

protected:
  virtual auto on_new_sub_pipeline() -> Pipeline* = 0;

private:
  class Stream;
  class InitialStream;

  //
  // Server::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public StreamBase,
    public EventSource
  {
    Stream(Server *server, int id);
    pjs::Ref<Pipeline> m_pipeline;
    void event(Event *evt) override { EventSource::output(evt); }
    void on_event(Event *evt) override  { StreamBase::on_event(evt); }
    friend class Server;
    friend class InitialStream;
  };

  //
  // Server::InitialStream
  //

  class InitialStream :
    public pjs::Pooled<InitialStream>,
    public EventTarget
  {
  public:
    InitialStream(Server *server)
      : m_server(server) {}

    void start();

    virtual void on_event(Event *evt) override;

  private:
    Server* m_server = nullptr;
    pjs::Ref<http::RequestHead> m_head;
    Data m_body;
    bool m_started = false;
  };

  InitialStream* m_initial_stream = nullptr;

  virtual void on_event(Event *evt) override { Endpoint::on_event(evt); }
  virtual void on_output(Event *evt) override { EventFunction::output(evt); }
  virtual auto on_new_stream(int id) -> StreamBase* override { return new Stream(this, id); }
  virtual void on_delete_stream(StreamBase *stream) override { delete static_cast<Stream*>(stream); }
};

//
// Client
//

class Client :
  public Endpoint,
  public EventSource
{
public:
  Client();

  void open(EventFunction *session);
  auto stream() -> EventFunction*;
  void close(EventFunction *stream);
  void go_away();

private:
  class Stream;

  int m_last_sent_stream_id = -1;

  //
  // Client::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public StreamBase,
    public EventFunction
  {
    Stream(Client *client, int id);
    void on_event(Event *evt) override { StreamBase::on_event(evt); }
    void event(Event *evt) override { EventFunction::output(evt); }
    friend class Client;
  };

  virtual void on_event(Event *evt) override { Endpoint::on_event(evt); }
  virtual void on_output(Event *evt) override { EventSource::output(evt); }
  virtual auto on_new_stream(int id) -> StreamBase* override { return new Stream(this, id); }
  virtual void on_delete_stream(StreamBase *stream) override { /* do not delete it here  */ }
};

} // namespace http2
} // namespace pipy

#endif // HTTP2_HPP
