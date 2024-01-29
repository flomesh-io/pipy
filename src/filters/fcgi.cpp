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
  Deframer::read(8, m_header);
}

auto Endpoint::request(int id) -> Request* {
  auto p = m_requests.get(id);
  return p ? *p : nullptr;
}

auto Endpoint::request_open(int id) -> Request* {
  if (!id) id = m_requests.alloc();
  auto r = on_new_request(id);
  *m_requests.get(id) = r;
  return r;
}

void Endpoint::request_close(Request *request) {
  m_requests.free(request->id());
  on_delete_request(request);
}

void Endpoint::process_event(Event *evt) {
  if (auto data = evt->as<Data>()) {
    Deframer::deframe(*data);
  } else if (auto eos = evt->as<StreamEnd>()) {
  }
}

void Endpoint::shutdown() {
}

auto Endpoint::on_state(int state, int c) -> int {
  switch (state) {
    case STATE_RECORD_HEADER: {
      auto hdr = (const FCGI_Header *)m_header;
      auto size = ((int)hdr->requestIdB1 << 8) + hdr->requestIdB0 + hdr->paddingLength;
      m_decoding_record_type = hdr->type;
      m_decoding_request_id = ((int)hdr->requestIdB1 << 8) + hdr->requestIdB0;
      m_decoding_padding_length = hdr->paddingLength;
      m_decoding_buffer.clear();
      if (size > 0) {
        Deframer::read(size, &m_decoding_buffer);
        return STATE_RECORD_BODY;
      } else {
        on_record(hdr->type, m_decoding_request_id, m_decoding_buffer);
        Deframer::read(8, m_header);
        return STATE_RECORD_HEADER;
      }
    }
    case STATE_RECORD_BODY: {
      if (m_decoding_padding_length > 0) m_decoding_buffer.pop(m_decoding_padding_length);
      on_record(m_decoding_record_type, m_decoding_request_id, m_decoding_buffer);
      Deframer::read(8, m_header);
      return STATE_RECORD_HEADER;
    }
    default: Deframer::pass_all(true); return -1;
  }
}

//
// Client
//

auto Client::begin() -> EventFunction* {
  return nullptr;
}

void Client::abort(EventFunction *request) {
}

void Client::shutdown() {
}

void Client::on_output(Event *evt) {
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
  return new Request(id);
}

void Client::on_delete_request(Endpoint::Request *request) {
  delete static_cast<Request*>(request);
}

//
// Client::Request
//

void Client::Request::receive_end(Data &data) {
}

void Client::Request::receive_stdout(Data &data) {
}

void Client::Request::receive_stderr(Data &data) {
}

//
// Server
//

void Server::shutdown() {
}

void Server::on_output(Event *evt) {
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

Mux::Mux(const Mux &r) {
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

void Mux::Session::mux_session_open(MuxSource *source) {
  Client::chain(MuxSession::input());
  MuxSession::chain(Client::reply());
}

auto Mux::Session::mux_session_open_stream(MuxSource *source) -> EventFunction* {
  return Client::begin();
}

void Mux::Session::mux_session_close_stream(EventFunction *stream) {
  Client::abort(stream);
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
  field<int>("flags", [](RequestHead *obj) { return &obj->flags; });
  field<Ref<Array>>("params", [](RequestHead *obj) { return &obj->params; });
}

template<> void ClassDef<ResponseTail>::init() {
  field<int>("appStatus", [](ResponseTail *obj) { return &obj->appStatus; });
  field<int>("protocolStatus", [](ResponseTail *obj) { return &obj->protocolStatus; });
  field<Ref<pipy::Data>>("params", [](ResponseTail *obj) { return &obj->stderr_data; });
}

} // namespace pjs
