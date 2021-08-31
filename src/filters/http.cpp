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

void Decoder::input(
  const pjs::Ref<Data> &data,
  const std::function<void(MessageHead*)> &on_message_start
) {
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
        m_output(output);
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
            output_start(on_message_start);
            if (is_bodiless_response()) {
              output_end();
              state = HEAD;
            } else {
              state = CHUNK_HEAD;
            }
          } else {
            if (content_length.is_string()) {
              m_body_size = std::atoi(content_length.s()->c_str());
            }
            if (m_body_size > 0) {
              output_start(on_message_start);
              if (is_bodiless_response()) {
                output_end();
                state = HEAD;
              } else {
                state = BODY;
              }
            } else {
              output_start(on_message_start);
              output_end();
              state = HEAD;
            }
          }
        }
        break;
      }
      case HEAD:
        if (m_state != HEAD) {
          output_end();
        }
        break;
      default: break;
    }

    m_state = state;
  }
}

void Decoder::end(SessionEnd *end) {
  if (m_is_response && (m_state == HEAD || m_state == HEADER) && end->error()) {
    int status_code = 0;
    pjs::Str *status_text = nullptr;
    switch (end->error()) {
    case SessionEnd::CANNOT_RESOLVE:
      status_code = 502;
      status_text = s_cannot_resolve;
      break;
    case SessionEnd::CONNECTION_REFUSED:
      status_code = 502;
      status_text = s_connection_refused;
      break;
    case SessionEnd::UNAUTHORIZED:
      status_code = 401;
      status_text = s_unauthorized;
      break;
    case SessionEnd::READ_ERROR:
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
    m_output(MessageStart::make(head));
    m_output(MessageEnd::make());
  }
  m_output(end);
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

void Encoder::input(const pjs::Ref<Event> &evt) {
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
        m_output(buf);
      } else {
        m_output(data);
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      if (is_bodiless_response()) {
        output_head();
      } else if (m_chunked) {
        m_output(s_dp.make("0\r\n\r\n"));
      } else if (m_buffer) {
        m_content_length = m_buffer->size();
        output_head();
        if (!m_buffer->empty()) m_output(m_buffer);
      }
      m_output(evt);
      if (m_is_response && m_is_final) {
        m_output(SessionEnd::make());
      }
    }
    reset();

  } else if (evt->is<SessionEnd>()) {
    m_output(evt);
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

  m_output(m_start);
  m_output(buffer);
}

//
// RequestDecoder
//

RequestDecoder::RequestDecoder()
  : m_decoder(false, [this](Event *evt) { output(evt); })
{
}

RequestDecoder::RequestDecoder(const RequestDecoder &r)
  : RequestDecoder()
{
}

RequestDecoder::~RequestDecoder()
{
}

auto RequestDecoder::help() -> std::list<std::string> {
  return {
    "decodeHTTPRequest()",
    "Deframes an HTTP request message",
  };
}

void RequestDecoder::dump(std::ostream &out) {
  out << "decodeHTTPRequest";
}

auto RequestDecoder::clone() -> Filter* {
  return new RequestDecoder(*this);
}

void RequestDecoder::reset() {
  m_decoder.reset();
  m_session_end = false;
}

void RequestDecoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  // Data
  if (auto data = inp->as<Data>()) {
    m_decoder.input(data);

  // End of session
  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
    output(inp);
  }
}

//
// ResponseDecoder
//

ResponseDecoder::ResponseDecoder()
  : m_decoder(true, [this](Event *evt) { output(evt); })
{
}

ResponseDecoder::ResponseDecoder(pjs::Object *options)
  : m_decoder(true, [this](Event *evt) { output(evt); })
{
  if (options) {
    options->get("bodiless", m_bodiless);
  }
}

ResponseDecoder::ResponseDecoder(const std::function<bool()> &bodiless)
  : m_decoder(true, [this](Event *evt) { output(evt); })
  , m_bodiless_func(bodiless)
{
}

ResponseDecoder::ResponseDecoder(const ResponseDecoder &r)
  : m_decoder(true, [this](Event *evt) { output(evt); })
  , m_bodiless(r.m_bodiless)
  , m_bodiless_func(r.m_bodiless_func)
{
}

ResponseDecoder::~ResponseDecoder()
{
}

auto ResponseDecoder::help() -> std::list<std::string> {
  return {
    "decodeHTTPResponse([options])",
    "Deframes an HTTP response message",
    "options = <object> Options currently including only bodiless"
  };
}

void ResponseDecoder::dump(std::ostream &out) {
  out << "decodeHTTPResponse";
}

void ResponseDecoder::reset() {
  m_decoder.reset();
  m_session_end = false;
}

auto ResponseDecoder::clone() -> Filter* {
  return new ResponseDecoder(*this);
}

void ResponseDecoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  // Data
  if (auto data = inp->as<Data>()) {
    m_decoder.input(
      data,
      [=](MessageHead*) {
        m_decoder.set_bodiless(is_bodiless(ctx));
      }
    );

  // End of session
  } else if (auto end = inp->as<SessionEnd>()) {
    m_session_end = true;

    if (end->error()) {
      int status_code = 0;
      const char *status_text = nullptr;
      switch (end->error()) {
        case SessionEnd::CANNOT_RESOLVE:
          status_code = 503;
          status_text = "Cannot Resolve";
          break;
        case SessionEnd::CONNECTION_REFUSED:
          status_code = 503;
          status_text = "Connection Refused";
          break;
        case SessionEnd::UNAUTHORIZED:
          status_code = 401;
          status_text = "Unauthorized";
          break;
        case SessionEnd::READ_ERROR:
          status_code = 502;
          status_text = "Read Error";
          break;
        default:
          status_code = 500;
          status_text = "Internal Server Error";
          break;
      }
      auto head = ResponseHead::make();
      head->headers(pjs::Object::make());
      head->protocol(s_http_1_1);
      head->status(status_code);
      head->status_text(pjs::Str::make(status_text));
      output(MessageStart::make(head));
      output(MessageEnd::make());
    }

    output(inp);
  }
}

bool ResponseDecoder::is_bodiless(Context *ctx) {
  if (m_bodiless_func) {
    return m_bodiless_func();
  } else {
    pjs::Value ret;
    eval(*ctx, m_bodiless, ret);
    return ret.to_boolean();
  }
}

//
// RequestEncoder
//

RequestEncoder::RequestEncoder()
  : m_encoder(false, [this](Event *evt) { output(evt); })
{
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : RequestEncoder()
{
}

RequestEncoder::~RequestEncoder()
{
}

auto RequestEncoder::help() -> std::list<std::string> {
  return {
    "encodeHTTPRequest()",
    "Frames an HTTP request message",
  };
}

void RequestEncoder::dump(std::ostream &out) {
  out << "encodeHTTPRequest";
}

auto RequestEncoder::clone() -> Filter* {
  return new RequestEncoder(*this);
}

void RequestEncoder::reset() {
  m_encoder.reset();
  m_session_end = false;
}

void RequestEncoder::process(Context *ctx, Event *inp) {
  static Data::Producer s_dp("encodeHTTPRequest");

  if (m_session_end) return;

  if (inp->is<SessionEnd>()) {
    m_session_end = true;
    output(inp);
  } else {
    m_encoder.input(inp);
  }
}

//
// ResponseEncoder
//

ResponseEncoder::ResponseEncoder()
  : m_encoder(true, [this](Event *evt) { output(evt); })
{
}

ResponseEncoder::ResponseEncoder(pjs::Object *options)
  : m_encoder(true, [this](Event *evt) { output(evt); })
{
  if (options) {
    options->get("bodiless", m_bodiless);
  }
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : m_encoder(true, [this](Event *evt) { output(evt); })
  , m_bodiless(r.m_bodiless)
{
}

ResponseEncoder::~ResponseEncoder()
{
}

auto ResponseEncoder::help() -> std::list<std::string> {
  return {
    "encodeHTTPResponse([options])",
    "Frames an HTTP response message",
    "options = <object> Options currently including only bodiless",
  };
}

void ResponseEncoder::dump(std::ostream &out) {
  out << "encodeHTTPResponse";
}

auto ResponseEncoder::clone() -> Filter* {
  return new ResponseEncoder(*this);
}

void ResponseEncoder::reset() {
  m_encoder.reset();
  m_session_end = false;
}

void ResponseEncoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (inp->is<SessionEnd>()) {
    m_session_end = true;
    output(inp);
  } else {
    if (inp->is<MessageStart>()) {
      pjs::Value bodiless;
      if (!eval(*ctx, m_bodiless, bodiless)) return;
      m_encoder.set_bodiless(bodiless.to_boolean());
    }
    m_encoder.input(inp);
  }
}

//
// ServerConnection
//

class ServerConnection : public pjs::Pooled<ServerConnection> {
public:
  ServerConnection(
    const std::function<Session*()> &new_session,
    const Event::Receiver &on_output
  )
    : m_new_session(new_session)
    , m_decoder(false, [this](Event *evt) { feed(evt); })
    , m_encoder(true, on_output) {}

  ~ServerConnection() {
    while (!m_queue.empty()) {
      auto channel = m_queue.front();
      m_queue.pop();
      delete channel;
    }
  }

  void input(Data *data) {
    m_decoder.input(data);
  }

private:
  struct Stream : public pjs::Pooled<Stream> {
    pjs::Ref<Session> session;
    EventBuffer buffer;
    bool input_end = false;
    bool output_end = false;
    bool bodiless = false;
    bool final = false;
    ~Stream() { session->on_output(nullptr); }
  };

  std::function<Session*()> m_new_session;
  Decoder m_decoder;
  Encoder m_encoder;
  std::queue<Stream*> m_queue;

  void feed(Event *evt) {
    if (auto start = evt->as<MessageStart>()) {
      auto stream = new Stream;
      auto session = m_new_session();
      session->on_output([=](Event *inp) {
        if (stream->output_end) return;
        if (inp->is<SessionEnd>()) {
          stream->buffer.push(MessageEnd::make());
          stream->output_end = true;
        } else {
          stream->buffer.push(inp);
          if (inp->is<MessageEnd>()) {
            stream->output_end = true;
          }
        }
        pump();
      });
      stream->session = session;
      stream->bodiless = m_decoder.is_bodiless();
      stream->final = m_decoder.is_final();
      m_queue.push(stream);
      session->input(evt);

    } else if (!m_queue.empty()) {
      auto stream = m_queue.back();
      if (!stream->input_end) {
        auto is_end = evt->is<MessageEnd>();
        stream->session->input(evt); // evt is gone after this
        if (is_end) {
          stream->input_end = true;
        }
      }
    }
  }

  void pump() {

    // Flush all completed streams in the front
    while (!m_queue.empty()) {
      auto stream = m_queue.front();
      if (!stream->output_end) break;
      m_queue.pop();
      auto &buffer = stream->buffer;
      if (!buffer.empty()) {
        m_encoder.set_bodiless(stream->bodiless);
        m_encoder.set_final(stream->final);
        buffer.flush([this](Event *evt) {
          m_encoder.input(evt);
        });
      }
      delete stream;
    }

    // Flush the first stream in queue
    if (!m_queue.empty()) {
      auto stream = m_queue.front();
      auto &buffer = stream->buffer;
      if (!buffer.empty()) {
        m_encoder.set_bodiless(stream->bodiless);
        m_encoder.set_final(stream->final);
        buffer.flush([this](Event *evt) {
          m_encoder.input(evt);
        });
      }
    }
  }
};

//
// Demux
//

Demux::Demux()
{
}

Demux::Demux(Pipeline *pipeline)
  : m_pipeline(pipeline)
{
}

Demux::Demux(pjs::Str *target)
  : m_target(target)
{
}

Demux::Demux(const Demux &r)
  : m_pipeline(r.m_pipeline)
  , m_target(r.m_target)
{
}

Demux::~Demux()
{
}

auto Demux::help() -> std::list<std::string> {
  return {
    "demuxHTTP(target)",
    "Deframes HTTP requests, sends each to a separate pipeline session, and frames their responses",
    "target = <string> Name of the pipeline to receive deframed requests",
  };
}

void Demux::dump(std::ostream &out) {
  out << "demuxHTTP";
}

auto Demux::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = false;
  return "demuxHTTP";
}

void Demux::bind() {
  if (!m_pipeline) {
    m_pipeline = pipeline(m_target);
  }
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::reset() {
  delete m_connection;
  m_connection = nullptr;
  m_session_end = false;
}

void Demux::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (!m_connection) {
    auto worker = pipeline()->module()->worker();
    auto pipeline = m_pipeline;
    m_connection = new ServerConnection(
      [=]() { return Session::make(worker->new_runtime_context(ctx), pipeline); },
      [=](Event *evt) { output(evt); }
    );
  }

  if (auto data = inp->as<Data>()) {
    m_connection->input(data);

  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
  }
}

//
// ClientConnection
//

class ClientConnection : public MuxBase::Connection, public pjs::Pooled<ClientConnection> {
public:
  ClientConnection()
    : m_encoder(false, [this](Event *evt) { send(evt); })
    , m_decoder(true, [this](Event *evt) { feed(evt); }) {}

private:
  class Stream : public Connection::Stream, public pjs::Pooled<Stream> {
  public:
    Stream(ClientConnection *connection, MessageStart *start, const Event::Receiver &on_output)
      : m_connection(connection)
      , m_start(start)
      , m_buffer(Data::make())
      , m_output(on_output) {}

  private:
    virtual void input(Data *data) override {
      m_buffer->push(*data);
      m_connection->pump();
    }

    virtual void end() override {
      m_end = MessageEnd::make();
      m_connection->pump();
    }

    virtual void close() override {
      if (!m_input_end) end();
      m_output = nullptr;
      if (!m_queued) delete this;
    }

    ClientConnection* m_connection;
    Event::Receiver m_output;
    pjs::Ref<MessageStart> m_start;
    pjs::Ref<MessageEnd> m_end;
    pjs::Ref<Data> m_buffer;
    bool m_queued = true;
    bool m_input_end = false;
    bool m_output_end = false;
    bool m_bodiless = false;

    friend class ClientConnection;
  };

  Encoder m_encoder;
  Decoder m_decoder;
  std::list<Stream*> m_queue;

  virtual auto stream(MessageStart *start, const Event::Receiver &on_output) -> Connection::Stream* override {
    auto s = new Stream(this, start, on_output);
    m_queue.push_back(s);
    pump();
    return s;
  }

  virtual void receive(Event *evt) override {
    if (auto data = evt->as<Data>()) {
      for (auto *stream : m_queue) {
        if (!stream->m_output_end) {
          m_decoder.set_bodiless(stream->m_bodiless);
          break;
        }
      }
      m_decoder.input(data);
    } else if (auto end = evt->as<SessionEnd>()) {
      m_decoder.end(end);
      reset();
    }
  }

  virtual void close() override {
    delete this;
  }

  void pump() {
    if (!m_queue.empty()) {
      auto stream = m_queue.front();
      if (!stream->m_input_end) {
        auto &start = stream->m_start;
        auto &data = stream->m_buffer;
        auto &end = stream->m_end;
        if (start) {
          m_encoder.input(start);
          stream->m_bodiless = m_encoder.is_bodiless();
          start = nullptr;
        }
        if (!data->empty()) {
          m_encoder.input(data);
          data = Data::make();
        }
        if (end) {
          m_encoder.input(end);
          end = nullptr;
          stream->m_input_end = true;
        }
      }
      clean();
    }
  }

  void feed(const pjs::Ref<Event> &evt) {
    for (auto *stream : m_queue) {
      if (!stream->m_output_end) {
        auto &output = stream->m_output;
        if (output) output(evt);
        if (evt->is<MessageEnd>()) {
          if (m_decoder.is_final()) reset();
          stream->m_output_end = true;
          clean();
          pump();
        }
        break;
      }
    }
  }

  void clean() {
    while (!m_queue.empty()) {
      auto stream = m_queue.front();
      if (!stream->m_input_end || !stream->m_output_end) break;
      m_queue.pop_front();
      if (stream->m_output) {
        stream->m_queued = false;
      } else {
        delete stream;
      }
    }
  }

  ~ClientConnection() {
    for (auto *stream : m_queue) {
      delete stream;
    }
  }

  friend class Stream;
};

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(Pipeline *pipeline, const pjs::Value &channel)
  : MuxBase(pipeline, channel)
{
}

Mux::Mux(pjs::Str *target, const pjs::Value &channel)
  : MuxBase(target, channel)
{
}

Mux::Mux(const Mux &r)
  : MuxBase(r)
{
}

Mux::~Mux()
{
}

auto Mux::help() -> std::list<std::string> {
  return {
    "muxHTTP(target[, channel])",
    "Frames HTTP requests, send to a new or shared pipeline session, and deframes its responses",
    "target = <string> Name of the pipeline to receive framed requests",
    "channel = <number|string|function> Key of the shared pipeline session",
  };
}

void Mux::dump(std::ostream &out) {
  out << "muxHTTP";
}

auto Mux::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(target()->str());
  fork = false;
  return "muxHTTP";
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::new_connection() -> Connection* {
  return new ClientConnection;
}

//
// Server
//

Server::Server()
  : m_decoder(false, [this](Event *evt) { request(evt); })
  , m_encoder(true, [this](Event *evt) { output(evt); })
{
}

Server::Server(const std::function<Message*(Context*, Message*)> &handler)
  : m_decoder(false, [this](Event *evt) { request(evt); })
  , m_encoder(true, [this](Event *evt) { output(evt); })
  , m_handler_func(handler)
{
}

Server::Server(pjs::Object *handler)
  : m_decoder(false, [this](Event *evt) { request(evt); })
  , m_encoder(true, [this](Event *evt) { output(evt); })
  , m_handler(handler)
{
}

Server::Server(const Server &r)
  : m_decoder(false, [this](Event *evt) { request(evt); })
  , m_encoder(true, [this](Event *evt) { output(evt); })
  , m_handler_func(r.m_handler_func)
  , m_handler(r.m_handler)
{
}

Server::~Server()
{
}

auto Server::help() -> std::list<std::string> {
  return {
    "serveHTTP(handler)",
    "Serves HTTP requests with a handler function",
    "handler = <function> Function that returns a response message",
  };
}

void Server::dump(std::ostream &out) {
  out << "serveHTTP";
}

auto Server::clone() -> Filter* {
  return new Server(*this);
}

void Server::reset() {
  m_decoder.reset();
  m_encoder.reset();
  m_context = nullptr;
  m_head = nullptr;
  m_body = nullptr;
  m_queue.clear();
  m_session_end = false;
}

void Server::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (auto data = inp->as<Data>()) {
    m_context = ctx;
    m_decoder.input(data);

  } else if (inp->is<SessionEnd>()) {
    output(inp);
    m_session_end = true;
  }
}

void Server::request(const pjs::Ref<Event> &evt) {
  if (auto start = evt->as<MessageStart>()) {
    m_head = start->head();
    m_body = Data::make();

    m_queue.push_back({
      m_decoder.is_bodiless(),
      m_decoder.is_final(),
    });

  } else if (auto data = evt->as<Data>()) {
    if (m_body) {
      m_body->push(*data);
    }

  } else if (evt->is<MessageEnd>()) {
    if (!m_queue.empty()) {
      auto &req = m_queue.front();
      m_encoder.set_bodiless(req.is_bodiless);
      m_encoder.set_final(req.is_final);
      m_queue.pop_front();

      pjs::Ref<Message> msg(Message::make(m_head, m_body));

      if (m_handler_func) {
        msg = m_handler_func(m_context, msg);
      } else if (m_handler && m_handler->is_function()) {
        pjs::Value arg(msg), ret;
        if (!callback(*m_context, m_handler->as<pjs::Function>(), 1, &arg, ret)) return;
        if (ret.is_object()) {
          msg = ret.o();
        } else {
          msg = nullptr;
          if (!ret.is_undefined()) {
            Log::error("[serveHTTP] handler did not return a valid message");
          }
        }
      } else if (m_handler && m_handler->is_instance_of<Message>()) {
        msg = m_handler->as<Message>();
      } else {
        msg = nullptr;
      }

      if (msg) {
        pjs::Ref<MessageStart> start(MessageStart::make(msg->head()));
        m_encoder.input(start);
        if (auto *body = msg->body()) m_encoder.input(body);
        m_encoder.input(evt);
      } else {
        pjs::Ref<MessageStart> start(MessageStart::make());
        m_encoder.input(start);
        m_encoder.input(evt);
      }
    }

    m_head = nullptr;
    m_body = nullptr;
  }
}

} // namespace http
} // namespace pipy