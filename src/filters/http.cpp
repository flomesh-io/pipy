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

#include "http.hpp"
#include "api/http.hpp"
#include "compress.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "inbound.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <queue>

namespace pipy {
namespace http {

static const pjs::Ref<pjs::Str> s_protocol(pjs::Str::make("protocol"));
static const pjs::Ref<pjs::Str> s_method(pjs::Str::make("method"));
static const pjs::Ref<pjs::Str> s_head(pjs::Str::make("HEAD"));
static const pjs::Ref<pjs::Str> s_path(pjs::Str::make("path"));
static const pjs::Ref<pjs::Str> s_status(pjs::Str::make("status"));
static const pjs::Ref<pjs::Str> s_status_text(pjs::Str::make("statusText"));
static const pjs::Ref<pjs::Str> s_headers(pjs::Str::make("headers"));
static const pjs::Ref<pjs::Str> s_http_1_0(pjs::Str::make("HTTP/1.0"));
static const pjs::Ref<pjs::Str> s_http_1_1(pjs::Str::make("HTTP/1.1"));
static const pjs::Ref<pjs::Str> s_connection(pjs::Str::make("connection"));
static const pjs::Ref<pjs::Str> s_keep_alive(pjs::Str::make("keep-alive"));
static const pjs::Ref<pjs::Str> s_close(pjs::Str::make("close"));
static const pjs::Ref<pjs::Str> s_transfer_encoding(pjs::Str::make("transfer-encoding"));
static const pjs::Ref<pjs::Str> s_content_length(pjs::Str::make("content-length"));
static const pjs::Ref<pjs::Str> s_content_encoding(pjs::Str::make("content-encoding"));
static const pjs::Ref<pjs::Str> s_bad_gateway(pjs::Str::make("Bad Gateway"));
static const pjs::Ref<pjs::Str> s_cannot_resolve(pjs::Str::make("Cannot Resolve"));
static const pjs::Ref<pjs::Str> s_connection_refused(pjs::Str::make("Connection Refused"));
static const pjs::Ref<pjs::Str> s_unauthorized(pjs::Str::make("Unauthorized"));
static const pjs::Ref<pjs::Str> s_read_error(pjs::Str::make("Read Error"));

// HTTP status code as in:
// https://www.iana.org/assignments/http-status-codes/http-status-codes.txt

static const char *s_status_1xx[] = {
  "Continue",            // 100 [RFC7231, Section 6.2.1]
  "Switching Protocols", // 101 [RFC7231, Section 6.2.2]
  "Processing",          // 102 [RFC2518]
  "Early Hints",         // 103 [RFC8297]
};

static const char *s_status_2xx[] = {
  "OK",                            // 200 [RFC7231, Section 6.3.1]
  "Created",                       // 201 [RFC7231, Section 6.3.2]
  "Accepted",                      // 202 [RFC7231, Section 6.3.3]
  "Non-Authoritative Information", // 203 [RFC7231, Section 6.3.4]
  "No Content",                    // 204 [RFC7231, Section 6.3.5]
  "Reset Content",                 // 205 [RFC7231, Section 6.3.6]
  "Partial Content",               // 206 [RFC7233, Section 4.1]
  "Multi-Status",                  // 207 [RFC4918]
  "Already Reported",              // 208 [RFC5842]
  0,                               // 209
  0,                               // 210
  0,                               // 211
  0,                               // 212
  0,                               // 213
  0,                               // 214
  0,                               // 215
  0,                               // 216
  0,                               // 217
  0,                               // 218
  0,                               // 219
  0,                               // 220
  0,                               // 221
  0,                               // 222
  0,                               // 223
  0,                               // 224
  0,                               // 225
  "IM Used",                       // 226 [RFC3229]
};

static const char *s_status_3xx[] = {
  "Multiple Choices",   // 300 [RFC7231, Section 6.4.1]
  "Moved Permanently",  // 301 [RFC7231, Section 6.4.2]
  "Found",              // 302 [RFC7231, Section 6.4.3]
  "See Other",          // 303 [RFC7231, Section 6.4.4]
  "Not Modified",       // 304 [RFC7232, Section 4.1]
  "Use Proxy",          // 305 [RFC7231, Section 6.4.5]
  0,                    // 306 [RFC7231, Section 6.4.6]
  "Temporary Redirect", // 307 [RFC7231, Section 6.4.7]
  "Permanent Redirect", // 308 [RFC7538]
};

static const char *s_status_4xx[] = {
  "Bad Request",                     // 400 [RFC7231, Section 6.5.1]
  "Unauthorized",                    // 401 [RFC7235, Section 3.1]
  "Payment Required",                // 402 [RFC7231, Section 6.5.2]
  "Forbidden",                       // 403 [RFC7231, Section 6.5.3]
  "Not Found",                       // 404 [RFC7231, Section 6.5.4]
  "Method Not Allowed",              // 405 [RFC7231, Section 6.5.5]
  "Not Acceptable",                  // 406 [RFC7231, Section 6.5.6]
  "Proxy Authentication Required",   // 407 [RFC7235, Section 3.2]
  "Request Timeout",                 // 408 [RFC7231, Section 6.5.7]
  "Conflict",                        // 409 [RFC7231, Section 6.5.8]
  "Gone",                            // 410 [RFC7231, Section 6.5.9]
  "Length Required",                 // 411 [RFC7231, Section 6.5.10]
  "Precondition Failed",             // 412 [RFC7232, Section 4.2][RFC8144, Section 3.2]
  "Payload Too Large",               // 413 [RFC7231, Section 6.5.11]
  "URI Too Long",                    // 414 [RFC7231, Section 6.5.12]
  "Unsupported Media Type",          // 415 [RFC7231, Section 6.5.13][RFC7694, Section 3]
  "Range Not Satisfiable",           // 416 [RFC7233, Section 4.4]
  "Expectation Failed",              // 417 [RFC7231, Section 6.5.14]
  0,                                 // 418
  0,                                 // 419
  0,                                 // 420
  "Misdirected Request",             // 421 [RFC7540, Section 9.1.2]
  "Unprocessable Entity",            // 422 [RFC4918]
  "Locked",                          // 423 [RFC4918]
  "Failed Dependency",               // 424 [RFC4918]
  "Too Early",                       // 425 [RFC8470]
  "Upgrade Required",                // 426 [RFC7231, Section 6.5.15]
  0,                                 // 427
  "Precondition Required",           // 428 [RFC6585]
  "Too Many Requests",               // 429 [RFC6585]
  "Unassigned",                      // 430
  "Request Header Fields Too Large", // 431 [RFC6585]
  0,                                 // 432
  0,                                 // 433
  0,                                 // 434
  0,                                 // 435
  0,                                 // 436
  0,                                 // 437
  0,                                 // 438
  0,                                 // 439
  0,                                 // 440
  0,                                 // 441
  0,                                 // 442
  0,                                 // 443
  0,                                 // 444
  0,                                 // 445
  0,                                 // 446
  0,                                 // 447
  0,                                 // 448
  0,                                 // 449
  0,                                 // 450
  "Unavailable For Legal Reasons"    // 451 [RFC7725]
};

static const char *s_status_5xx[] = {
  "Internal Server Error",           // 500 [RFC7231, Section 6.6.1]
  "Not Implemented",                 // 501 [RFC7231, Section 6.6.2]
  "Bad Gateway",                     // 502 [RFC7231, Section 6.6.3]
  "Service Unavailable",             // 503 [RFC7231, Section 6.6.4]
  "Gateway Timeout",                 // 504 [RFC7231, Section 6.6.5]
  "HTTP Version Not Supported",      // 505 [RFC7231, Section 6.6.6]
  "Variant Also Negotiates",         // 506 [RFC2295]
  "Insufficient Storage",            // 507 [RFC4918]
  "Loop Detected",                   // 508 [RFC5842]
  "Unassigned",                      // 509
  "Not Extended",                    // 510 [RFC2774]
  "Network Authentication Required", // 511 [RFC6585]
};

static struct {
  const char **names;
  size_t count;
} s_status_name_map[] = {
  { s_status_1xx, sizeof(s_status_1xx) / sizeof(char*) },
  { s_status_2xx, sizeof(s_status_2xx) / sizeof(char*) },
  { s_status_3xx, sizeof(s_status_3xx) / sizeof(char*) },
  { s_status_4xx, sizeof(s_status_4xx) / sizeof(char*) },
  { s_status_5xx, sizeof(s_status_5xx) / sizeof(char*) },
};

static auto lookup_status_text(int status) -> const char* {
  auto i = status / 100;
  auto j = status % 100;
  if (1 <= i && i <= 5) {
    const auto &l = s_status_name_map[i-1];
    if (j >= l.count) return nullptr;
    return l.names[j];
  }
  return nullptr;
}

//
// Decoder
//

void Decoder::reset() {
  m_state = HEAD;
  m_head_buffer.clear();
  m_head = nullptr;
  m_body_size = 0;
  m_is_bodiless = false;
  m_is_final = false;
}

void Decoder::on_event(Event *evt) {
  if (auto e = evt->as<StreamEnd>()) {
    stream_end(e);
    reset();
    return;
  }

  auto data = evt->as<Data>();
  if (!data) return;

  while (!data->empty()) {
    auto state = m_state;
    pjs::Ref<Data> output(Data::make());

    data->shift_to(
      [&](int c) -> bool {
        switch (state) {
        case HEAD:
          if (c == '\n') {
            state = HEAD_EOL;
            return true;
          }
          return false;

        case HEADER:
          if (c == '\n') {
            state = HEADER_EOL;
            return true;
          }
          return false;

        case BODY:
          m_body_size--;
          if (m_body_size == 0) {
            state = HEAD;
            return true;
          }
          return false;

        case CHUNK_HEAD:
          if (c == '\n') {
            if (m_body_size > 0) {
              state = CHUNK_BODY;
              return true;
            } else {
              state = CHUNK_LAST;
              return false;
            }
          }
          else if ('0' <= c && c <= '9') m_body_size = (m_body_size << 4) + (c - '0');
          else if ('a' <= c && c <= 'f') m_body_size = (m_body_size << 4) + (c - 'a') + 10;
          else if ('A' <= c && c <= 'F') m_body_size = (m_body_size << 4) + (c - 'A') + 10;
          return false;

        case CHUNK_BODY:
          m_body_size--;
          if (m_body_size == 0) {
            state = CHUNK_TAIL;
            return true;
          }
          return false;

        case CHUNK_TAIL:
          if (c == '\n') {
            state = CHUNK_HEAD;
            m_body_size = 0;
          }
          return false;

        case CHUNK_LAST:
          if (c == '\n') {
            state = HEAD;
            return true;
          }
          return false;

        case HEAD_EOL:
        case HEADER_EOL:
          return false;
        }
      },
      *output
    );

    switch (m_state) {
      case HEAD:
      case HEADER:
        if (m_head_buffer.size() + output->size() > MAX_HEADER_SIZE) {
          auto room = MAX_HEADER_SIZE - m_head_buffer.size();
          output->pop(output->size() - room);
        }
        m_head_buffer.push(*output);
        break;
      case BODY:
      case CHUNK_BODY:
        EventFunction::output(output);
        break;
      default: break;
    }

    switch (state) {
      case HEAD_EOL: {
        auto len = m_head_buffer.size();
        char buf[len + 1];
        m_head_buffer.to_bytes((uint8_t *)buf);
        buf[len] = 0;
        std::string segs[3];
        char *str = buf;
        for (int i = 0; i < 3; i++) {
          if (auto p = std::strpbrk(str, " \r\n")) {
            segs[i].assign(str, p - str);
            str = p + 1;
          } else {
            break;
          }
        }
        if (m_is_response) {
          auto res = ResponseHead::make();
          res->protocol(pjs::Str::make(segs[0]));
          res->status(std::atoi(segs[1].c_str()));
          res->status_text(pjs::Str::make(segs[2]));
          m_head = res;
        } else {
          auto req = RequestHead::make();
          auto method = pjs::Str::make(segs[0]);
          req->method(method);
          req->path(pjs::Str::make(segs[1]));
          req->protocol(pjs::Str::make(segs[2]));
          m_head = req;
          m_is_bodiless = (method == s_head);
        }
        m_head->headers(pjs::Object::make());
        state = HEADER;
        m_head_buffer.clear();
        break;
      }
      case HEADER_EOL: {
        auto len = m_head_buffer.size();
        if (len > 2) {
          char buf[len + 1];
          m_head_buffer.to_bytes((uint8_t *)buf);
          buf[len] = 0;
          if (auto p = std::strchr(buf, ':')) {
            std::string name(buf, p - buf);
            p++; while (*p && std::isblank(*p)) p++;
            if (auto q = std::strpbrk(p, "\r\n")) {
              std::string value(p, q - p);
              for (auto &c : name) c = std::tolower(c);
              m_head->headers()->set(name, value);
            }
          }
          state = HEADER;
          m_head_buffer.clear();

        } else {
          m_body_size = 0;
          m_head_buffer.clear();

          pjs::Value connection, transfer_encoding, content_length;
          pjs::Object* headers = m_head->headers();
          headers->get(s_connection, connection);
          headers->get(s_transfer_encoding, transfer_encoding);
          headers->get(s_content_length, content_length);
          if (!is_bodiless_response()) headers->ht_delete(s_content_length);

          // Connection and Keep-Alive
          if (connection.is_string()) {
            m_is_final = (connection.s() == s_close);
          } else {
            m_is_final = (m_head->protocol() == s_http_1_0);
          }

          // Transfer-Encoding and Content-Length
          if (transfer_encoding.is_string() && !strncmp(transfer_encoding.s()->c_str(), "chunked", 7)) {
            message_start();
            if (is_bodiless_response()) {
              message_end();
              state = HEAD;
            } else {
              state = CHUNK_HEAD;
            }
          } else {
            if (content_length.is_string()) {
              m_body_size = std::atoi(content_length.s()->c_str());
            }
            if (m_body_size > 0) {
              message_start();
              if (is_bodiless_response()) {
                message_end();
                state = HEAD;
              } else {
                state = BODY;
              }
            } else {
              message_start();
              message_end();
              state = HEAD;
            }
          }
        }
        break;
      }
      case HEAD:
        if (m_state != HEAD) {
          message_end();
        }
        break;
      default: break;
    }

    m_state = state;
  }
}

void Decoder::stream_end(StreamEnd *end) {
  if (m_is_response && (m_state == HEAD || m_state == HEADER) && end->error()) {
    int status_code = 0;
    pjs::Str *status_text = nullptr;
    switch (end->error()) {
    case StreamEnd::CANNOT_RESOLVE:
      status_code = 502;
      status_text = s_cannot_resolve;
      break;
    case StreamEnd::CONNECTION_REFUSED:
      status_code = 502;
      status_text = s_connection_refused;
      break;
    case StreamEnd::UNAUTHORIZED:
      status_code = 401;
      status_text = s_unauthorized;
      break;
    case StreamEnd::READ_ERROR:
      status_code = 502;
      status_text = s_read_error;
      break;
    default:
      status_code = 502;
      status_text = s_bad_gateway;
      break;
    }
    auto head = ResponseHead::make();
    head->headers(pjs::Object::make());
    head->protocol(s_http_1_1);
    head->status(status_code);
    head->status_text(status_text);
    output(MessageStart::make(head));
    output(end);
  } else {
    output(end);
  }
}

//
// Encoder
//

Data::Producer Encoder::s_dp("HTTP Encoder");

void Encoder::reset() {
  m_start = nullptr;
  m_buffer = nullptr;
  m_is_bodiless = false;
  m_is_final = false;
}

void Encoder::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    m_start = start;
    m_buffer = nullptr;
    m_content_length = -1;
    m_chunked = false;

    if (auto head = start->head()) {
      pjs::Value method, headers, transfer_encoding, content_length;
      if (!m_is_response) {
        head->get(s_method, method);
        if (method.is_string() && method.s() == s_head) {
          m_is_bodiless = true;
        }
      }
      head->get(s_headers, headers);
      if (headers.is_object()) {
        headers.o()->get(s_transfer_encoding, transfer_encoding);
        if (transfer_encoding.is_string()) {
          if (!std::strncmp(transfer_encoding.s()->c_str(), "chunked", 7)) {
            m_chunked = true;
          }
        }
        if (!m_chunked) {
          headers.o()->get(s_content_length, content_length);
          if (content_length.is_string()) {
            m_content_length = std::atoi(content_length.s()->c_str());
          } else if (content_length.is_number()) {
            m_content_length = content_length.n();
          }
        }
      }
    }

    if (is_bodiless_response()) {
      m_content_length = 0;
    } else if (m_chunked || m_content_length >= 0) {
      output_head();
    } else {
      m_buffer = Data::make();
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      if (is_bodiless_response()) {
        m_content_length += data->size();
      } else if (m_buffer) {
        m_buffer->push(*data);
      } else if (m_chunked && !data->empty()) {
        auto buf = Data::make();
        char str[100];
        std::sprintf(str, "%X\r\n", data->size());
        s_dp.push(buf, str);
        buf->push(*data);
        s_dp.push(buf, "\r\n");
        output(buf);
      } else {
        output(data);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      if (is_bodiless_response()) {
        output_head();
      } else if (m_chunked) {
        output(s_dp.make("0\r\n\r\n"));
      } else if (m_buffer) {
        m_content_length = m_buffer->size();
        output_head();
        if (!m_buffer->empty()) output(m_buffer);
      }
      output(evt);
      if (m_is_response && m_is_final) {
        output(StreamEnd::make());
      }
    }
    reset();

  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

void Encoder::output_head() {
  auto buffer = Data::make();

  if (m_is_response) {
    pjs::Value protocol, status, status_text;
    if (auto head = m_start->head()) {
      head->get(s_protocol, protocol);
      head->get(s_status, status);
      head->get(s_status_text, status_text);
    }

    if (protocol.is_string()) {
      s_dp.push(buffer, protocol.s()->str());
      s_dp.push(buffer, ' ');
    } else {
      s_dp.push(buffer, "HTTP/1.1 ");
    }

    int status_num = 200;
    if (status.is_number()) {
      status_num = status.n();
    } else if (status.is_string()) {
      status_num = std::atoi(status.s()->c_str());
    }

    char status_str[100];
    std::sprintf(status_str, "%d ", status_num);
    s_dp.push(buffer, status_str);

    if (status_text.is_string()) {
      s_dp.push(buffer, status_text.s()->str());
      s_dp.push(buffer, "\r\n");
    } else {
      if (auto str = lookup_status_text(status_num)) {
        s_dp.push(buffer, str);
        s_dp.push(buffer, "\r\n");
      } else {
        s_dp.push(buffer, "OK\r\n");
      }
    }

  } else {
    pjs::Value method, path, protocol;
    if (auto head = m_start->head()) {
      head->get(s_method, method);
      head->get(s_path, path);
      head->get(s_protocol, protocol);
    }

    if (method.is_string()) {
      s_dp.push(buffer, method.s()->str());
      s_dp.push(buffer, ' ');
    } else {
      s_dp.push(buffer, "GET ");
    }

    if (path.is_string()) {
      s_dp.push(buffer, path.s()->str());
      s_dp.push(buffer, ' ');
    } else {
      s_dp.push(buffer, "/ ");
    }

    if (protocol.is_string()) {
      s_dp.push(buffer, protocol.s()->str());
      s_dp.push(buffer, "\r\n");
    } else {
      s_dp.push(buffer, "HTTP/1.1\r\n");
    }
  }

  pjs::Value headers;
  if (auto head = m_start->head()) {
    head->get(s_headers, headers);
  }

  bool content_length_written = false;

  if (headers.is_object()) {
    headers.o()->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        if (k == s_connection || k == s_keep_alive) return;
        if (k == s_content_length && m_chunked) return;
        s_dp.push(buffer, k->str());
        s_dp.push(buffer, ": ");
        if (k == s_content_length) {
          content_length_written = true;
          if (!is_bodiless_response()) {
            char str[100];
            std::sprintf(str, "%d\r\n", m_content_length);
            s_dp.push(buffer, str);
            return;
          }
        }
        auto s = v.to_string();
        s_dp.push(buffer, s->str());
        s_dp.push(buffer, "\r\n");
        s->release();
      }
    );
  }

  if (!content_length_written && !m_chunked && (m_content_length > 0 || m_is_response)) {
    char str[100];
    std::sprintf(str, ": %d\r\n", m_content_length);
    s_dp.push(buffer, s_content_length->str());
    s_dp.push(buffer, str);
  }

  if (m_is_final) {
    static std::string str("connection: close\r\n");
    s_dp.push(buffer, str);
  } else {
    static std::string str("connection: keep-alive\r\n");
    s_dp.push(buffer, str);
  }

  s_dp.push(buffer, "\r\n");

  output(m_start);
  output(buffer);
}

//
// RequestDecoder
//

RequestDecoder::RequestDecoder()
  : m_ef_decode(false)
{
}

RequestDecoder::RequestDecoder(const RequestDecoder &r)
  : Filter(r)
  , m_ef_decode(false)
{
}

RequestDecoder::~RequestDecoder()
{
}

void RequestDecoder::dump(std::ostream &out) {
  out << "decodeHTTPRequest";
}

auto RequestDecoder::clone() -> Filter* {
  return new RequestDecoder(*this);
}

void RequestDecoder::chain() {
  Filter::chain();
  m_ef_decode.chain(output());
}

void RequestDecoder::reset() {
  Filter::reset();
  m_ef_decode.reset();
}

void RequestDecoder::process(Event *evt) {
  if (auto data = evt->as<Data>()) {
    output(evt, m_ef_decode.input());
  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

//
// ResponseDecoder
//

ResponseDecoder::ResponseDecoder(pjs::Object *options)
  : m_ef_decode(true)
  , m_ef_set_bodiless(this)
{
  if (options) {
    options->get("bodiless", m_bodiless);
  }
}

ResponseDecoder::ResponseDecoder(const ResponseDecoder &r)
  : Filter(r)
  , m_ef_decode(true)
  , m_ef_set_bodiless(this)
  , m_bodiless(r.m_bodiless)
{
}

ResponseDecoder::~ResponseDecoder()
{
}

void ResponseDecoder::dump(std::ostream &out) {
  out << "decodeHTTPResponse";
}

void ResponseDecoder::chain() {
  Filter::chain();
  m_ef_decode.chain(m_ef_set_bodiless.input());
  m_ef_set_bodiless.chain(output());
}

void ResponseDecoder::reset() {
  Filter::reset();
  m_ef_decode.reset();
}

auto ResponseDecoder::clone() -> Filter* {
  return new ResponseDecoder(*this);
}

void ResponseDecoder::process(Event *evt) {
  output(evt, m_ef_decode.input());
}

void ResponseDecoder::on_set_bodiless(Event *evt) {
  if (evt->is<MessageStart>()) {
    pjs::Value ret;
    eval(m_bodiless, ret);
    m_ef_decode.set_bodiless(ret.to_boolean());
  }
}

//
// RequestEncoder
//

RequestEncoder::RequestEncoder()
  : m_ef_encode(false)
{
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : Filter(r)
  , m_ef_encode(false)
{
}

RequestEncoder::~RequestEncoder()
{
}

void RequestEncoder::dump(std::ostream &out) {
  out << "encodeHTTPRequest";
}

auto RequestEncoder::clone() -> Filter* {
  return new RequestEncoder(*this);
}

void RequestEncoder::chain() {
  Filter::chain();
  m_ef_encode.chain(output());
}

void RequestEncoder::reset() {
  Filter::reset();
  m_ef_encode.reset();
}

void RequestEncoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    output(evt);
  } else {
    output(evt, m_ef_encode.input());
  }
}

//
// ResponseEncoder
//

ResponseEncoder::ResponseEncoder(pjs::Object *options)
  : m_ef_encode(true)
{
  if (options) {
    options->get("bodiless", m_bodiless);
  }
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : Filter(r)
  , m_ef_encode(true)
  , m_bodiless(r.m_bodiless)
{
}

ResponseEncoder::~ResponseEncoder()
{
}

void ResponseEncoder::dump(std::ostream &out) {
  out << "encodeHTTPResponse";
}

auto ResponseEncoder::clone() -> Filter* {
  return new ResponseEncoder(*this);
}

void ResponseEncoder::chain() {
  Filter::chain();
  m_ef_encode.chain(output());
}

void ResponseEncoder::reset() {
  Filter::reset();
  m_ef_encode.reset();
}

void ResponseEncoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    output(evt);
  } else {
    if (evt->is<MessageStart>()) {
      pjs::Value bodiless;
      if (!eval(m_bodiless, bodiless)) return;
      m_ef_encode.set_bodiless(bodiless.to_boolean());
    }
    output(evt, m_ef_encode.input());
  }
}

//
// RequestEnqueue
//

void RequestEnqueue::on_event(Event *evt) {
  if (evt->is<MessageStart>()) {
    auto *q = static_cast<RequestQueue*>(this);
    auto *r = new RequestQueue::Request;
    q->on_enqueue(r);
    q->m_queue.push(r);
  }
  output(evt);
}

//
// RequestDequeue
//

void RequestDequeue::on_event(Event *evt) {
  if (evt->is<MessageStart>()) {
    auto *q = static_cast<RequestQueue*>(this);
    if (auto *r = q->m_queue.head()) {
      q->on_dequeue(r);
      q->m_queue.remove(r);
      delete r;
    }
  } else if (evt->is<StreamEnd>()) {
    auto *q = static_cast<RequestQueue*>(this);
    q->reset();
  }
  output(evt);
}

//
// RequestQueue
//

void RequestQueue::reset() {
  while (auto *r = m_queue.head()) {
    m_queue.remove(r);
    delete r;
  }
}

//
// Demux
//

Demux::Demux()
  : m_ef_decoder(false)
  , m_ef_encoder(true)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , m_ef_decoder(false)
  , m_ef_encoder(true)
{
}

Demux::~Demux()
{
}

void Demux::dump(std::ostream &out) {
  out << "demuxHTTP";
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::chain() {
  Filter::chain();
  m_ef_decoder.chain(RequestEnqueue::input());
  RequestEnqueue::chain(DemuxFunction::input());
  DemuxFunction::chain(RequestDequeue::input());
  RequestDequeue::chain(m_ef_encoder.input());
  m_ef_encoder.chain(Filter::output());
}

void Demux::reset() {
  Filter::reset();
  RequestQueue::reset();
  m_ef_decoder.reset();
  m_ef_encoder.reset();
}

void Demux::process(Event *evt) {
  Filter::output(evt, m_ef_decoder.input());
}

auto Demux::on_new_sub_pipeline() -> Pipeline* {
  return sub_pipeline(0, true);
}

void Demux::on_enqueue(Request *req) {
  req->is_bodiless = m_ef_decoder.is_bodiless();
  req->is_final = m_ef_decoder.is_final();
}

void Demux::on_dequeue(Request *req) {
  m_ef_encoder.set_bodiless(req->is_bodiless);
  m_ef_encoder.set_final(req->is_final);
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(const pjs::Value &key)
  : pipy::Mux(key)
{
}

Mux::Mux(const Mux &r)
  : pipy::Mux(r)
{
}

Mux::~Mux()
{
}

void Mux::dump(std::ostream &out) {
  out << "muxHTTP";
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

//
// Mux::Session
//

void Mux::Session::open(Pipeline *pipeline) {
  m_ef_encoder.chain(RequestEnqueue::input());
  RequestEnqueue::chain(pipeline->input());
  pipeline->chain(m_ef_decoder.input());
  m_ef_decoder.chain(RequestDequeue::input());
  RequestDequeue::chain(Demux::input());
}

void Mux::Session::input(Event *evt) {
  m_ef_encoder.input()->input(evt);
}

void Mux::Session::on_enqueue(Request *req) {
  req->is_bodiless = m_ef_encoder.is_bodiless();
}

void Mux::Session::on_dequeue(Request *req) {
  m_ef_decoder.set_bodiless(req->is_bodiless);
}

void Mux::Session::close() {
  pipy::Mux::Session::close();
  RequestQueue::reset();
}

//
// Server
//

Server::Server(const std::function<Message*(Message*)> &handler)
  : m_ef_decoder(false)
  , m_ef_encoder(true)
  , m_ef_handler(this)
  , m_handler_func(handler)
{
}

Server::Server(pjs::Object *handler)
  : m_ef_decoder(false)
  , m_ef_encoder(true)
  , m_ef_handler(this)
  , m_handler_obj(handler)
{
}

Server::Server(const Server &r)
  : Filter(r)
  , m_ef_decoder(false)
  , m_ef_encoder(true)
  , m_ef_handler(this)
  , m_handler_func(r.m_handler_func)
  , m_handler_obj(r.m_handler_obj)
{
}

Server::~Server()
{
}

void Server::dump(std::ostream &out) {
  out << "serveHTTP";
}

auto Server::clone() -> Filter* {
  return new Server(*this);
}

void Server::chain() {
  Filter::chain();
  m_ef_decoder.chain(m_ef_handler.input());
  m_ef_handler.chain(m_ef_encoder.input());
  m_ef_encoder.chain(output());
}

void Server::reset() {
  Filter::reset();
  m_ef_decoder.reset();
  m_ef_encoder.reset();
}

void Server::process(Event *evt) {
  output(evt, m_ef_decoder.input());
}

void Server::Handler::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
      m_buffer.clear();
      auto &encoder = m_server->m_ef_encoder;
      auto &decoder = m_server->m_ef_decoder;
      encoder.set_bodiless(decoder.is_bodiless());
      encoder.set_final(decoder.is_final());
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      pjs::Ref<Message> msg(
        Message::make(
          m_start->head(),
          Data::make(m_buffer)
        )
      );

      m_start = nullptr;
      m_buffer.clear();

      if (auto &func = m_server->m_handler_func) {
        msg = func(msg);

      } else if (auto &handler = m_server->m_handler_obj) {
        if (handler->is_instance_of<Message>()) {
          msg = handler->as<Message>();

        } else if (handler->is_function()) {
          pjs::Value arg(msg), ret;
          if (!m_server->callback(handler->as<pjs::Function>(), 1, &arg, ret)) return;
          if (ret.is_object()) {
            msg = ret.o();
          } else {
            msg = nullptr;
            if (!ret.is_undefined()) {
              Log::error("[serveHTTP] handler did not return a valid message");
            }
          }

        } else {
          msg = nullptr;
        }
      }

      if (msg) {
        output(MessageStart::make(msg->head()));
        if (auto *body = msg->body()) output(body);
        output(evt);
      } else {
        output(MessageStart::make());
        output(evt);
      }
    }
  }
}

} // namespace http
} // namespace pipy
