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

#include "fcgi.hpp"

namespace pipy {
namespace fcgi {

thread_local static Data::Producer s_dp("FastCGI");

//
// The following macro definitions and structures
// are taken from the FastCGI Specification
//

#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11

#define FCGI_KEEP_CONN  1
#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

#define FCGI_REQUEST_COMPLETE 0
#define FCGI_CANT_MPX_CONN    1
#define FCGI_OVERLOADED       2
#define FCGI_UNKNOWN_ROLE     3

typedef struct {
  unsigned char version;
  unsigned char type;
  unsigned char requestIdB1;
  unsigned char requestIdB0;
  unsigned char contentLengthB1;
  unsigned char contentLengthB0;
  unsigned char paddingLength;
  unsigned char reserved;
} FCGI_Header;

typedef struct {
  unsigned char roleB1;
  unsigned char roleB0;
  unsigned char flags;
  unsigned char reserved[5];
} FCGI_BeginRequestBody;

typedef struct {
  unsigned char appStatusB3;
  unsigned char appStatusB2;
  unsigned char appStatusB1;
  unsigned char appStatusB0;
  unsigned char protocolStatus;
  unsigned char reserved[3];
} FCGI_EndRequestBody;

typedef struct {
  unsigned char type;
  unsigned char reserved[7];
} FCGI_UnknownTypeBody;

//
// Endpoint
//

void Endpoint::reset() {
  Deframer::reset(STATE_RECORD_HEADER);
  Deframer::pass_all(false);
  Deframer::read(sizeof(FCGI_Header), m_header);
  for (auto r = m_request_list.head(); r; r = r->next()) {
    m_requests.free(r->id());
    on_delete_request(r);
  }
  m_request_list.clear();
  m_decoding_buffer = nullptr;
}

auto Endpoint::request(int id) -> Request* {
  auto p = m_requests.get(id);
  return p ? *p : nullptr;
}

auto Endpoint::request_open(int id) -> Request* {
  if (!id) id = m_requests.alloc();
  auto r = on_new_request(id);
  auto p = m_requests.get(id);
  m_request_list.push(r);
  return (*p = r);
}

void Endpoint::request_close(Request *request) {
  m_request_list.remove(request);
  m_requests.free(request->id());
  on_delete_request(request);
}

void Endpoint::process_event(Event *evt) {
  if (auto data = evt->as<Data>()) {
    Deframer::deframe(*data);
  } else if (auto eos = evt->as<StreamEnd>()) {
  }
}

void Endpoint::send_record(int type, int request_id, const void *body, size_t size) {
  int padding = 0;
  Data::Builder db(m_sending_buffer, &s_dp);
  write_record_header(db, type, request_id, size, padding);
  if (size > 0) db.push(body, size);
  if (padding > 0) db.push(uint8_t(0), size_t(padding));
  db.flush();
  FlushTarget::need_flush();
}

void Endpoint::send_record(int type, int request_id, Data &body) {
  int padding = 0;
  Data::Builder db(m_sending_buffer, &s_dp);
  write_record_header(db, type, request_id, body.size(), padding);
  if (body.size() > 0) db.push(std::move(body));
  if (padding > 0) db.push(uint8_t(0), size_t(padding));
  db.flush();
  FlushTarget::need_flush();
}

void Endpoint::shutdown() {
}

void Endpoint::write_record_header(
  Data::Builder &db,
  int type,
  int request_id,
  int length,
  int &padding
) {
  padding = (length + 7) / 8 * 8 - length;
  FCGI_Header hdr;
  hdr.version = 1;
  hdr.type = type;
  hdr.requestIdB1 = uint8_t(request_id >> 8);
  hdr.requestIdB0 = uint8_t(request_id >> 0);
  hdr.contentLengthB1 = uint8_t(length >> 8);
  hdr.contentLengthB0 = uint8_t(length >> 0);
  hdr.paddingLength = padding;
  hdr.reserved = 0;
  db.push(&hdr, sizeof(hdr));
}

auto Endpoint::on_state(int state, int c) -> int {
  switch (state) {
    case STATE_RECORD_HEADER: {
      auto hdr = (const FCGI_Header *)m_header;
      auto size = ((int)hdr->contentLengthB1 << 8) + hdr->contentLengthB0 + hdr->paddingLength;
      m_decoding_record_type = hdr->type;
      m_decoding_request_id = ((int)hdr->requestIdB1 << 8) + hdr->requestIdB0;
      m_decoding_padding_length = hdr->paddingLength;
      m_decoding_buffer = Data::make();
      if (size > 0) {
        Deframer::read(size, m_decoding_buffer.get());
        return STATE_RECORD_BODY;
      } else {
        on_record(hdr->type, m_decoding_request_id, *m_decoding_buffer);
        Deframer::read(sizeof(FCGI_Header), m_header);
        return STATE_RECORD_HEADER;
      }
    }
    case STATE_RECORD_BODY: {
      if (m_decoding_padding_length > 0) m_decoding_buffer->pop(m_decoding_padding_length);
      on_record(m_decoding_record_type, m_decoding_request_id, *m_decoding_buffer);
      Deframer::read(sizeof(FCGI_Header), m_header);
      return STATE_RECORD_HEADER;
    }
    default: Deframer::pass_all(true); return -1;
  }
}

void Endpoint::on_flush() {
  if (!m_sending_buffer.empty()) {
    on_output(Data::make(std::move(m_sending_buffer)));
  }
}

//
// Client
//

auto Client::open_request() -> EventFunction* {
  return static_cast<Request*>(Endpoint::request_open());
}

void Client::close_request(EventFunction *request) {
  Endpoint::request_close(static_cast<Request*>(request));
}

void Client::shutdown() {
  Endpoint::shutdown();
}

void Client::on_record(int type, int request_id, Data &body) {
  switch (type) {
    case FCGI_END_REQUEST:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_end(body);
      }
      break;
    case FCGI_STDOUT:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_stdout(body);
      }
      break;
    case FCGI_STDERR:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_stderr(body);
      }
      break;
    default: break;
  }
}

auto Client::on_new_request(int id) -> Endpoint::Request* {
  return new Request(this, id);
}

void Client::on_delete_request(Endpoint::Request *request) {
  delete static_cast<Request*>(request);
}

void Client::on_output(Event *evt) {
  EventSource::output(evt);
}

void Client::on_reply(Event *evt) {
  Endpoint::process_event(evt);
}

//
// Client::Request
//

void Client::Request::receive_end(Data &data) {
  if (!m_response_started) {
    m_response_started = true;
    EventFunction::output(MessageStart::make());
  }

  if (!m_response_ended) {
    m_response_ended = true;

    Data buf; data.shift(8, buf);
    uint8_t bytes[8]; buf.to_bytes(bytes);
    auto hdr = (const FCGI_EndRequestBody *)bytes;
    auto tail = ResponseTail::make();
    tail->appStatus = (
      ((uint32_t)hdr->appStatusB3 << 24) |
      ((uint32_t)hdr->appStatusB2 << 16) |
      ((uint32_t)hdr->appStatusB1 <<  8) |
      ((uint32_t)hdr->appStatusB0 <<  0)
    );
    tail->protocolStatus = hdr->protocolStatus;
    tail->stderr_data = Data::make(std::move(m_stderr_buffer));
    EventFunction::output(MessageEnd::make(tail));
  }
}

void Client::Request::receive_stdout(Data &data) {
  if (!m_response_started) {
    m_response_started = true;
    EventFunction::output(MessageStart::make());
  }

  if (!m_response_stdout_ended) {
    if (data.size() > 0) {
      EventFunction::output(Data::make(std::move(data)));
    } else {
      m_response_stdout_ended = true;
    }
  }
}

void Client::Request::receive_stderr(Data &data) {
  if (!m_response_started) {
    m_response_started = true;
    EventFunction::output(MessageStart::make());
  }

  if (!m_response_stderr_ended) {
    if (data.size() > 0) {
      m_stderr_buffer.push(std::move(data));
    } else {
      m_response_stderr_ended = true;
    }
  }
}

void Client::Request::on_event(Event *evt) {
  if (auto ms = evt->as<MessageStart>()) {
    if (!m_request_started) {
      m_request_started = true;

      pjs::Ref<RequestHead> head = pjs::coerce<RequestHead>(ms->head());
      FCGI_BeginRequestBody body;
      std::memset(&body, 0, sizeof(body));
      body.roleB1 = head->role >> 8;
      body.roleB0 = head->role;
      body.flags = head->keepAlive ? FCGI_KEEP_CONN : 0;
      m_client->send_record(FCGI_BEGIN_REQUEST, id(), &body, sizeof(body));

      if (auto params = head->params.get()) {
        Data buf;
        Data::Builder db(buf, &s_dp);
        params->iterate_all(
          [&](pjs::Str *k, pjs::Value &v) {
            auto s = v.to_string();
            if (db.size() + k->size() + s->size() + 8 > 0xffff) {
              db.flush();
              m_client->send_record(FCGI_PARAMS, id(), buf);
              buf.clear();
              db.reset();
            }
            if (k->size() <= 127) {
              db.push(uint8_t(k->size()));
            } else {
              auto n = k->size() & 0x7fffffff;
              db.push(uint8_t(n >> 24) | uint8_t(0x80));
              db.push(uint8_t(n >> 16));
              db.push(uint8_t(n >> 8));
              db.push(uint8_t(n >> 0));
            }
            if (s->size() <= 127) {
              db.push(uint8_t(s->size()));
            } else {
              auto n = s->size() & 0x7fffffff;
              db.push(uint8_t(n >> 24) | uint8_t(0x80));
              db.push(uint8_t(n >> 16));
              db.push(uint8_t(n >> 8));
              db.push(uint8_t(n >> 0));
            }
            db.push(k->c_str(), k->size());
            db.push(s->c_str(), s->size());
            s->release();
          }
        );
        if (db.size() > 0) {
          db.flush();
          m_client->send_record(FCGI_PARAMS, id(), buf);
        }
      }

      m_client->send_record(FCGI_PARAMS, id(), nullptr, 0);
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_request_started && !m_request_ended) {
      if (data->size() > 0) {
        m_client->send_record(FCGI_STDIN, id(), *data);
      }
    }

  } else if (evt->is_end()) {
    if (m_request_started && !m_request_ended) {
      m_request_ended = true;
      m_client->send_record(FCGI_STDIN, id(), nullptr, 0);
    }
  }
}

//
// Server
//

void Server::shutdown() {
}

void Server::on_record(int type, int request_id, Data &body) {
  switch (type) {
    case FCGI_BEGIN_REQUEST:
      if (!request(request_id)) {
        auto r = static_cast<Request*>(request_open(request_id));
        r->receive_begin(body);
      }
      break;
    case FCGI_ABORT_REQUEST:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_abort();
      }
      break;
    case FCGI_PARAMS:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_params(body);
      }
      break;
    case FCGI_STDIN:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_stdin(body);
      }
      break;
    case FCGI_DATA:
      if (auto r = request(request_id)) {
        static_cast<Request*>(r)->receive_data(body);
      }
      break;
    default: break;
  }
}

auto Server::on_new_request(int id) -> Endpoint::Request* {
  return new Request(id);
}

void Server::on_delete_request(Endpoint::Request *request) {
  delete static_cast<Request*>(request);
}

void Server::on_output(Event *evt) {
  EventFunction::output(evt);
}

//
// Server::Request
//

void Server::Request::receive_begin(Data &data) {
  Data buf; data.shift(8, buf);
  uint8_t bytes[8]; buf.to_bytes(bytes);
  auto hdr = (const FCGI_BeginRequestBody *)bytes;
  m_role = ((int)hdr->roleB1 << 8) | hdr->roleB0;
  m_flags = hdr->flags;
}

void Server::Request::receive_abort() {
}

void Server::Request::receive_params(Data &data) {
}

void Server::Request::receive_stdin(Data &data) {
}

void Server::Request::receive_data(Data &data) {
}

//
// Demux
//

Demux::Demux()
{
}

Demux::Demux(const Demux &r) {
}

Demux::~Demux() {
}

void Demux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "demuxFastCGI";
  d.sub_type = Dump::DEMUX;
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::reset() {
  Filter::reset();
}

void Demux::process(Event *evt) {
}

void Demux::shutdown() {
}

auto Demux::on_demux_open_stream() -> EventFunction* {
  auto p = Filter::sub_pipeline(0, true);
  p->retain();
  p->start();
  return p;
}

void Demux::on_demux_close_stream(EventFunction *stream) {
  auto p = static_cast<Pipeline*>(stream);
  p->release();
}

void Demux::on_demux_complete() {
  if (auto eos = m_eos) {
    Filter::output(eos);
    m_eos = nullptr;
  }
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(pjs::Function *session_selector)
  : MuxBase(session_selector)
{
}

Mux::Mux(pjs::Function *session_selector, const MuxSession::Options &options)
  : MuxBase(session_selector)
  , m_options(options)
{
}

Mux::Mux(pjs::Function *session_selector, pjs::Function *options)
  : MuxBase(session_selector, options)
{
}

Mux::Mux(const Mux &r)
  : MuxBase(r)
  , m_options(r.m_options)
{
}

void Mux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "muxFastCGI";
  d.sub_type = Dump::MUX;
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* {
  return new SessionPool(options);
}

//
// Mux::Session
//

void Mux::Session::mux_session_open(MuxSource *) {
  Client::chain(MuxSession::input());
  MuxSession::chain(Client::reply());
}

auto Mux::Session::mux_session_open_stream(MuxSource *) -> EventFunction* {
  return Client::open_request();
}

void Mux::Session::mux_session_close_stream(EventFunction *stream) {
  Client::close_request(stream);
}

void Mux::Session::mux_session_close() {
  Client::shutdown();
}

} // namespace fcgi
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::fcgi;

template<> void ClassDef<RequestHead>::init() {
  field<int>("role", [](RequestHead *obj) { return &obj->role; });
  field<bool>("keepAlive", [](RequestHead *obj) { return &obj->keepAlive; });
  field<Ref<Object>>("params", [](RequestHead *obj) { return &obj->params; });
}

template<> void ClassDef<ResponseTail>::init() {
  field<int>("appStatus", [](ResponseTail *obj) { return &obj->appStatus; });
  field<int>("protocolStatus", [](ResponseTail *obj) { return &obj->protocolStatus; });
  field<Ref<pipy::Data>>("stderr", [](ResponseTail *obj) { return &obj->stderr_data; });
}

} // namespace pjs
