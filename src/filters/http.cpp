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
#include "log.hpp"

#include <queue>
#include <limits>

namespace pipy {
namespace http {

static const pjs::ConstStr s_protocol("protocol");
static const pjs::ConstStr s_method("method");
static const pjs::ConstStr s_GET("GET");
static const pjs::ConstStr s_HEAD("HEAD");
static const pjs::ConstStr s_CONNECT("CONNECT");
static const pjs::ConstStr s_path("path");
static const pjs::ConstStr s_status("status");
static const pjs::ConstStr s_status_text("statusText");
static const pjs::ConstStr s_headers("headers");
static const pjs::ConstStr s_http_1_0("HTTP/1.0");
static const pjs::ConstStr s_http_1_1("HTTP/1.1");
static const pjs::ConstStr s_connection("connection");
static const pjs::ConstStr s_keep_alive("keep-alive");
static const pjs::ConstStr s_set_cookie("set-cookie");
static const pjs::ConstStr s_close("close");
static const pjs::ConstStr s_transfer_encoding("transfer-encoding");
static const pjs::ConstStr s_content_length("content-length");
static const pjs::ConstStr s_content_encoding("content-encoding");
static const pjs::ConstStr s_upgrade("upgrade");
static const pjs::ConstStr s_websocket("websocket");
static const pjs::ConstStr s_h2c("h2c");
static const pjs::ConstStr s_bad_gateway("Bad Gateway");
static const pjs::ConstStr s_cannot_resolve("Cannot Resolve");
static const pjs::ConstStr s_connection_refused("Connection Refused");
static const pjs::ConstStr s_unauthorized("Unauthorized");
static const pjs::ConstStr s_read_error("Read Error");
static const pjs::ConstStr s_write_error("Write Error");
static const pjs::ConstStr s_gateway_timeout("Gateway Timeout");
static const pjs::ConstStr s_http2_preface_method("PRI");
static const pjs::ConstStr s_http2_preface_path("*");
static const pjs::ConstStr s_http2_preface_protocol("HTTP/2.0");

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
  m_header_transfer_encoding = nullptr;
  m_header_content_length = nullptr;
  m_header_connection = nullptr;
  m_header_upgrade = nullptr;
  m_body_size = 0;
  m_is_bodiless = false;
  m_is_switching = false;
  m_is_tunnel = false;
  m_has_error = false;
}

void Decoder::on_event(Event *evt) {
  if (m_state == HTTP2_PASS) {
    EventFunction::output(evt);
    return;
  }

  if (auto e = evt->as<StreamEnd>()) {
    stream_end(e);
    reset();
    return;
  }

  if (m_is_tunnel) {
    output(evt);
    return;
  }

  auto data = evt->as<Data>();
  if (!data) return;

  while (!m_has_error && !data->empty()) {
    auto state = m_state;
    pjs::Ref<Data> output(Data::make());

    // fast scan over the body
    if (state == BODY || state == CHUNK_BODY) {
      auto n = std::min(m_body_size, data->size());
      data->shift(n, *output);
      if (0 == (m_body_size -= n)) state = (state == BODY ? HEAD : CHUNK_TAIL);

    // byte scan the head
    } else {
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

          case HTTP2_PREFACE:
            if (!--m_body_size) {
              state = HTTP2_PASS;
              return true;
            }
            return false;

          case HTTP2_PASS:
            return false;

          default:
            // case BODY:
            // case CHUNK_BODY:
            // handle in the 'fast scan'
            return true;
          }
        },
        *output
      );
    }

    // old state
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

    // new state
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
          auto path = pjs::Str::make(segs[1]);
          auto protocol = pjs::Str::make(segs[2]);
          if (
            (s_http2_preface_method == method) &&
            (s_http2_preface_path == path) &&
            (s_http2_preface_protocol == protocol)
          ) {
            m_body_size = 8;
            state = HTTP2_PREFACE;
            break;
          } else if (protocol != s_http_1_0 && protocol != s_http_1_1) {
            m_has_error = true;
            on_decode_error();
            break;
          } else {
            req->method(method);
            req->path(path);
            req->protocol(protocol);
            m_head = req;
          }
        }
        m_head->headers(pjs::Object::make());
        m_header_transfer_encoding = nullptr;
        m_header_content_length = nullptr;
        m_header_connection = nullptr;
        m_header_upgrade = nullptr;
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
              pjs::Ref<pjs::Str> key(pjs::Str::make(name));
              pjs::Ref<pjs::Str> val(pjs::Str::make(value));
              auto headers = m_head->headers();
              if (key == s_set_cookie) {
                pjs::Value old;
                headers->get(key, old);
                if (old.is_array()) {
                  old.as<pjs::Array>()->push(val.get());
                } else if (old.is_string()) {
                  auto a = pjs::Array::make(2);
                  a->set(0, old.s());
                  a->set(1, val.get());
                  headers->set(key, a);
                } else {
                  headers->set(key, val.get());
                }
              } else {
                auto v = val.get();
                headers->set(key, val.get());
                if (key == s_transfer_encoding) m_header_transfer_encoding = v;
                else if (key == s_content_length) m_header_content_length = v;
                else if (key == s_connection) m_header_connection = v;
                else if (key == s_upgrade) m_header_upgrade = v;
              }
            }
          }
          state = HEADER;
          m_head_buffer.clear();

        } else {
          m_body_size = 0;
          m_head_buffer.clear();

          static std::string s_chunked("chunked");

          // Transfer-Encoding and Content-Length
          if (
            m_header_transfer_encoding &&
            utils::starts_with(m_header_transfer_encoding->str(), s_chunked)
          ) {
            message_start();
            if (is_bodiless_response()) {
              message_end();
              state = HEAD;
            } else {
              state = CHUNK_HEAD;
            }
          } else {
            message_start();
            if (m_header_content_length) {
              m_body_size = std::atoi(m_header_content_length->c_str());
            } else if (m_is_response && !m_is_bodiless) {
              auto status = m_head->as<ResponseHead>()->status();
              if (status >= 200 && status != 204 && status != 304) {
                m_body_size = std::numeric_limits<int>::max();
              }
            }
            if (m_body_size > 0) {
              if (is_bodiless_response()) {
                message_end();
                state = HEAD;
              } else {
                state = BODY;
              }
            } else {
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
      case HTTP2_PASS: {
        on_http2_pass();
        EventFunction::output(Data::make(std::move(*data)));
        break;
      }
      default: break;
    }

    if (m_is_tunnel) {
      EventFunction::output(Data::make(std::move(*data)));
    }

    m_state = state;
  }
}

void Decoder::message_start() {
  if (m_is_response) {
    on_decode_response(m_head->as<http::ResponseHead>());
  } else {
    on_decode_request(m_head->as<http::RequestHead>());
  }
  if (is_turning_tunnel()) on_decode_tunnel();
  output(MessageStart::make(m_head));
}

void Decoder::message_end() {
  output(MessageEnd::make());
  if (is_turning_tunnel()) m_is_tunnel = true;
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
    case StreamEnd::WRITE_ERROR:
      status_code = 502;
      status_text = s_write_error;
      break;
    case StreamEnd::CONNECTION_TIMEOUT:
    case StreamEnd::READ_TIMEOUT:
    case StreamEnd::WRITE_TIMEOUT:
      status_code = 504;
      status_text = s_gateway_timeout;
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

Encoder::Encoder(bool is_response)
  : m_prop_protocol(s_protocol)
  , m_prop_headers(s_headers)
  , m_prop_method(s_method)
  , m_prop_path(s_path)
  , m_prop_status(s_status)
  , m_prop_status_text(s_status_text)
  , m_is_response(is_response)
{
}

void Encoder::reset() {
  m_buffer.clear();
  m_start = nullptr;
  m_protocol = nullptr;
  m_method = nullptr;
  m_header_connection = nullptr;
  m_header_upgrade = nullptr;
  m_status_code = 0;
  m_is_final = false;
  m_is_bodiless = false;
  m_is_switching = false;
  m_is_tunnel = false;
}

void Encoder::on_event(Event *evt) {
  if (m_is_tunnel) {
    output(evt);
    return;
  }

  if (auto start = evt->as<MessageStart>()) {
    m_start = start;
    m_content_length = 0;
    m_chunked = false;
    m_buffer.clear();

    if (m_is_response) {
      m_status_code = 200;
      pjs::Value status;
      if (auto head = m_start->head()) m_prop_status.get(head, status);
      if (status.is_number()) {
        m_status_code = status.n();
      } else if (status.is_string()) {
        m_status_code = std::atoi(status.s()->c_str());
      }
      on_encode_response(start->head());
      if (is_turning_tunnel()) on_encode_tunnel();
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      if (is_bodiless_response()) {
        m_content_length += data->size();
      } else if (m_chunked) {
        if (!data->empty()) output_chunk(*data);
      } else {
        m_buffer.push(*data);
        m_content_length += data->size();
        if (m_buffer.size() > m_buffer_size) {
          m_chunked = true;
          output_head();
          output_chunk(m_buffer);
          m_buffer.clear();
        }
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      if (m_is_response && m_is_final) {
        output_end(StreamEnd::make());
      } else {
        output_end(evt);
      }
    }

    m_buffer.clear();
    m_start = nullptr;

  } else if (evt->is<StreamEnd>()) {
    output(evt);
    m_buffer.clear();
    m_start = nullptr;
  }
}

void Encoder::output_head() {
  auto buffer = Data::make();
  bool send_content_length = true;

  if (m_is_response) {
    pjs::Value protocol, status_text;
    if (auto head = m_start->head()) {
      m_prop_protocol.get(head, protocol);
      m_prop_status_text.get(head, status_text);
    }

    if (protocol.is_string()) {
      s_dp.push(buffer, protocol.s()->str());
      s_dp.push(buffer, ' ');
    } else {
      s_dp.push(buffer, "HTTP/1.1 ");
    }

    if (m_status_code < 200 || m_status_code == 204) {
      send_content_length = false;
    }

    char status_str[100];
    std::sprintf(status_str, "%d ", m_status_code);
    s_dp.push(buffer, status_str);

    if (status_text.is_string()) {
      s_dp.push(buffer, status_text.s()->str());
      s_dp.push(buffer, "\r\n");
    } else {
      if (auto str = lookup_status_text(m_status_code)) {
        s_dp.push(buffer, str);
        s_dp.push(buffer, "\r\n");
      } else {
        s_dp.push(buffer, "OK\r\n");
      }
    }

  } else {
    pjs::Value method, path, protocol;
    if (auto head = m_start->head()) {
      m_prop_method.get(head, method);
      m_prop_path.get(head, path);
      m_prop_protocol.get(head, protocol);
    }

    if (method.is_string()) {
      m_method = method.s();
      s_dp.push(buffer, method.s()->str());
      s_dp.push(buffer, ' ');
    } else {
      m_method = s_GET;
      s_dp.push(buffer, "GET ");
    }

    if (path.is_string()) {
      s_dp.push(buffer, path.s()->str());
      s_dp.push(buffer, ' ');
    } else {
      s_dp.push(buffer, "/ ");
    }

    if (protocol.is_string()) {
      m_protocol = protocol.s();
      s_dp.push(buffer, protocol.s()->str());
      s_dp.push(buffer, "\r\n");
    } else {
      m_protocol = s_http_1_1;
      s_dp.push(buffer, "HTTP/1.1\r\n");
    }
  }

  pjs::Value headers;
  if (auto head = m_start->head()) {
    m_prop_headers.get(head, headers);
  }

  bool content_length_written = false;

  if (headers.is_object()) {
    headers.o()->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        if (k == s_keep_alive) return;
        if (k == s_transfer_encoding) return;
        if (k == s_content_length) {
          if (is_bodiless_response()) {
            content_length_written = true;
          } else {
            return;
          }
        } else if (k == s_connection) {
          auto *s = v.to_string();
          auto is_upgrade = utils::iequals(s->str(), s_upgrade.get()->str());
          m_header_connection = s;
          s->release();
          if (!is_upgrade) return;
        } else if (k == s_upgrade) {
          auto *s = v.to_string();
          m_header_upgrade = s;
          s->release();
        }
        if (k == s_set_cookie && v.is_array()) {
          v.as<pjs::Array>()->iterate_all(
            [&](pjs::Value &v, int) {
              auto s = v.to_string();
              s_dp.push(buffer, k->str());
              s_dp.push(buffer, ": ");
              s_dp.push(buffer, s->str());
              s_dp.push(buffer, "\r\n");
              s->release();
            }
          );
        } else {
          s_dp.push(buffer, k->str());
          s_dp.push(buffer, ": ");
          auto s = v.to_string();
          s_dp.push(buffer, s->str());
          s_dp.push(buffer, "\r\n");
          s->release();
        }
      }
    );
  }

  if (!m_is_response) {
    on_encode_request(m_start->head());
  }

  if (send_content_length) {
    if (m_chunked) {
      static std::string str("transfer-encoding: chunked\r\n");
      s_dp.push(buffer, str);
    } else if (!content_length_written) {
      char str[100];
      std::sprintf(str, ": %d\r\n", m_content_length);
      s_dp.push(buffer, s_content_length.get()->str());
      s_dp.push(buffer, str);
    }

    if (m_is_final) {
      static std::string str("connection: close\r\n");
      s_dp.push(buffer, str);
    } else {
      static std::string str("connection: keep-alive\r\n");
      s_dp.push(buffer, str);
    }
  }

  s_dp.push(buffer, "\r\n");

  output(m_start);
  output(buffer);
}

void Encoder::output_chunk(const Data &data) {
  auto buf = Data::make();
  char str[100];
  std::sprintf(str, "%X\r\n", data.size());
  s_dp.push(buf, str);
  buf->push(data);
  s_dp.push(buf, "\r\n");
  output(buf);
}

void Encoder::output_end(Event *evt) {
  if (is_bodiless_response()) {
    output_head();
  } else if (m_chunked) {
    output(s_dp.make("0\r\n\r\n"));
  } else {
    output_head();
    if (!m_buffer.empty()) {
      output(Data::make(std::move(m_buffer)));
    }
  }
  output(evt);
  if (is_turning_tunnel()) m_is_tunnel = true;
  m_protocol = nullptr;
  m_method = nullptr;
  m_header_connection = nullptr;
  m_header_upgrade = nullptr;
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

void RequestDecoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decodeHTTPRequest";
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
// ResponseDecoder::Options
//

ResponseDecoder::Options::Options(pjs::Object *options) {
  Value(options, "bodiless")
    .get(bodiless)
    .get(bodiless_f)
    .check_nullable();
}

//
// ResponseDecoder
//

ResponseDecoder::ResponseDecoder(const Options &options)
  : Decoder(true)
  , m_options(options)
{
}

ResponseDecoder::ResponseDecoder(const ResponseDecoder &r)
  : Filter(r)
  , Decoder(true)
  , m_options(r.m_options)
{
}

ResponseDecoder::~ResponseDecoder()
{
}

void ResponseDecoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decodeHTTPResponse";
}

void ResponseDecoder::chain() {
  Filter::chain();
  Decoder::chain(Filter::output());
}

void ResponseDecoder::reset() {
  Filter::reset();
  Decoder::reset();
}

auto ResponseDecoder::clone() -> Filter* {
  return new ResponseDecoder(*this);
}

void ResponseDecoder::process(Event *evt) {
  Filter::output(evt, Decoder::input());
}

void ResponseDecoder::on_decode_response(http::ResponseHead *head) {
  if (m_options.bodiless_f) {
    pjs::Value ret;
    if (callback(m_options.bodiless_f, 0, nullptr, ret)) {
      Decoder::set_bodiless(ret.to_boolean());
    }
  } else {
    Decoder::set_bodiless(m_options.bodiless);
  }
}

//
// RequestEncoder
//

RequestEncoder::Options::Options(pjs::Object *options) {
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
}

//
// RequestEncoder
//

RequestEncoder::RequestEncoder(const Options &options)
  : Encoder(false)
  , m_options(options)
{
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : Filter(r)
  , Encoder(false)
  , m_options(r.m_options)
{
}

RequestEncoder::~RequestEncoder()
{
}

void RequestEncoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeHTTPRequest";
}

auto RequestEncoder::clone() -> Filter* {
  return new RequestEncoder(*this);
}

void RequestEncoder::chain() {
  Filter::chain();
  Encoder::chain(Filter::output());
  Encoder::set_buffer_size(m_options.buffer_size);
}

void RequestEncoder::reset() {
  Filter::reset();
  Encoder::reset();
}

void RequestEncoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  } else {
    Filter::output(evt, Encoder::input());
  }
}

//
// ResponseEncoder
//

ResponseEncoder::Options::Options(pjs::Object *options) {
  Value(options, "final")
    .get(final)
    .get(final_f)
    .check_nullable();
  Value(options, "bodiless")
    .get(bodiless)
    .get(bodiless_f)
    .check_nullable();
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
}

//
// ResponseEncoder
//

ResponseEncoder::ResponseEncoder(const Options &options)
  : Encoder(true)
  , m_options(options)
{
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : Filter(r)
  , Encoder(true)
  , m_options(r.m_options)
{
}

ResponseEncoder::~ResponseEncoder()
{
}

void ResponseEncoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeHTTPResponse";
}

auto ResponseEncoder::clone() -> Filter* {
  return new ResponseEncoder(*this);
}

void ResponseEncoder::chain() {
  Filter::chain();
  Encoder::chain(Filter::output());
  Encoder::set_buffer_size(m_options.buffer_size);
}

void ResponseEncoder::reset() {
  Filter::reset();
  Encoder::reset();
}

void ResponseEncoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  } else {
    if (evt->is<MessageStart>()) {
      if (m_options.final_f) {
        pjs::Value ret;
        if (callback(m_options.final_f, 0, nullptr, ret)) {
          Encoder::set_final(ret.to_boolean());
        }
      } else {
        Encoder::set_final(m_options.final);
      }
      if (m_options.bodiless_f) {
        pjs::Value ret;
        if (callback(m_options.bodiless_f, 0, nullptr, ret)) {
          Encoder::set_bodiless(ret.to_boolean());
        }
      } else {
        Encoder::set_bodiless(m_options.bodiless);
      }
    }
    Filter::output(evt, Encoder::input());
  }
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

void RequestQueue::push(Request *req) {
  m_queue.push(req);
}

auto RequestQueue::shift() -> Request* {
  auto req = m_queue.head();
  if (req) m_queue.remove(req);
  return req;
}

//
// RequestQueue
//

bool RequestQueue::Request::is_final() const {
  if (header_connection) {
    return (header_connection == s_close);
  } else {
    return (protocol == s_http_1_0);
  }
}

bool RequestQueue::Request::is_bodiless() const {
  return (
    method == s_HEAD ||
    method == s_CONNECT || (
      header_upgrade &&
      header_upgrade != s_h2c
    )
  );
}

bool RequestQueue::Request::is_switching() const {
  return (method == s_CONNECT || header_upgrade);
}

bool RequestQueue::Request::is_http2() const {
  return (header_upgrade == s_h2c);
}

//
// Demux::Options
//

Demux::Options::Options(pjs::Object *options)
  : http2::Endpoint::Options(options)
{
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
}

//
// Demux
//

Demux::Demux(const Options &options)
  : Decoder(false)
  , Encoder(true)
  , m_options(options)
  , m_prop_status(s_status)
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , Decoder(false)
  , Encoder(true)
  , m_options(r.m_options)
  , m_prop_status(s_status)
{
}

Demux::~Demux()
{
}

void Demux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "demuxHTTP";
  d.sub_type = Dump::DEMUX;
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::chain() {
  Filter::chain();
  Decoder::chain(QueueDemuxer::input());
  QueueDemuxer::chain(Encoder::input());
  Encoder::chain(Filter::output());
  Encoder::set_buffer_size(m_options.buffer_size);
}

void Demux::reset() {
  Filter::reset();
  Decoder::reset();
  Encoder::reset();
  QueueDemuxer::reset();
  m_request_queue.reset();
  if (m_http2_demuxer) {
    Decoder::chain(QueueDemuxer::input());
    delete m_http2_demuxer;
    m_http2_demuxer = nullptr;
  }
  m_shutdown = false;
}

void Demux::process(Event *evt) {
  Filter::output(evt, Decoder::input());
}

void Demux::shutdown() {
  Filter::shutdown();
  if (m_http2_demuxer) {
    m_http2_demuxer->go_away();
  } else if (m_request_queue.empty()) {
    Filter::output(StreamEnd::make());
  } else {
    m_shutdown = true;
  }
}

auto Demux::on_new_sub_pipeline(Input *chain_to) -> Pipeline* {
  return sub_pipeline(0, true, chain_to);
}

bool Demux::on_response_start(MessageStart *start) {
  int status;
  auto head = start->head();
  if (head && m_prop_status.get(head, status) && status == 100) {
    return false; // not the last response
  } else {
    return true;
  }
}

void Demux::on_decode_error() {
  Filter::output(StreamEnd::make());
}

void Demux::on_decode_request(http::RequestHead *head) {
  auto req = new RequestQueue::Request;
  req->protocol = head->protocol();
  req->method = head->method();
  req->header_connection = Decoder::header_connection();
  req->header_upgrade = Decoder::header_upgrade();
  m_request_queue.push(req);
  if (req->is_http2()) {
    auto head = ResponseHead::make();
    auto headers = pjs::Object::make();
    head->status(101);
    head->headers(headers);
    headers->set(s_connection, s_upgrade.get());
    headers->set(s_upgrade, s_h2c.get());
    auto inp = Encoder::input();
    inp->input(MessageStart::make(head));
    inp->input(MessageEnd::make());
    upgrade_http2();
    Decoder::chain(m_http2_demuxer->initial_stream());
  }
}

void Demux::on_encode_response(pjs::Object *head) {
  int status;
  if (head && m_prop_status.get(head, status) && status == 100) {
    if (auto req = m_request_queue.head()) {
      Encoder::set_bodiless(true);
    }
  } else if (auto req = m_request_queue.shift()) {
    Encoder::set_final(req->is_final() || (m_shutdown && m_request_queue.empty()));
    Encoder::set_bodiless(req->is_bodiless());
    Encoder::set_switching(req->is_switching());
    delete req;
  }
}

void Demux::on_encode_tunnel() {
  Decoder::set_tunnel(true);
  QueueDemuxer::isolate();
}

void Demux::on_http2_pass() {
  upgrade_http2();
  m_http2_demuxer->open();
  Decoder::chain(m_http2_demuxer->EventTarget::input());
}

void Demux::upgrade_http2() {
  if (!m_http2_demuxer) {
    m_http2_demuxer = new HTTP2Demuxer(this);
    m_http2_demuxer->chain(Filter::output());
  }
}

//
// Mux::Options
//

Mux::Options::Options(pjs::Object *options)
  : pipy::MuxQueue::Options(options)
  , http2::Endpoint::Options(options)
{
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
  Value(options, "version")
    .get(version)
    .get(version_f)
    .check_nullable();
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(pjs::Function *group)
  : pipy::MuxQueue(group)
{
}

Mux::Mux(pjs::Function *group, const Options &options)
  : pipy::MuxQueue(group, options)
  , m_options(options)
{
}

Mux::Mux(pjs::Function *group, pjs::Function *options)
  : pipy::MuxQueue(group, options)
  , m_options_f(options)
{
}

Mux::Mux(const Mux &r)
  : pipy::MuxQueue(r)
  , m_options(r.m_options)
  , m_options_f(r.m_options_f)
{
}

Mux::~Mux()
{
}

void Mux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "muxHTTP";
  d.sub_type = Dump::MUX;
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* {
  return new SessionCluster(this, options);
}

//
// Mux::Session
//

Mux::Session::~Session() {
  delete m_http2_muxer;
}

void Mux::Session::open() {
  select_protocol();
}

auto Mux::Session::open_stream() -> EventFunction* {
  if (m_http2_muxer) {
    return m_http2_muxer->stream();
  } else {
    return QueueMuxer::open();
  }
}

void Mux::Session::close_stream(EventFunction *stream) {
  if (m_http2_muxer) {
    m_http2_muxer->close(stream);
  } else {
    QueueMuxer::close(stream);
  }
}

void Mux::Session::close() {
  QueueMuxer::reset();
  m_request_queue.reset();
  if (m_http2_muxer) {
    InputContext ic;
    m_http2_muxer->go_away();
  }
  MuxBase::Session::close();
}

void Mux::Session::on_encode_request(pjs::Object *head) {
  auto *req = new RequestQueue::Request;
  req->protocol = Encoder::protocol();
  req->method = Encoder::method();
  req->header_connection = Encoder::header_connection();
  req->header_upgrade = Encoder::header_upgrade();
  m_request_queue.push(req);
}

void Mux::Session::on_decode_response(http::ResponseHead *head) {
  if (head->status() == 100) {
    if (auto *req = m_request_queue.head()) {
      Decoder::set_bodiless(true);
      QueueMuxer::increase_queue_count();
    }
  } else if (auto *req = m_request_queue.shift()) {
    Decoder::set_bodiless(req->is_bodiless());
    Decoder::set_switching(req->is_switching());
    delete req;
  }
}

void Mux::Session::on_decode_tunnel() {
  Encoder::set_tunnel(true);
  QueueMuxer::isolate();
}

void Mux::Session::on_decode_error()
{
}

void Mux::Session::on_notify() {
  select_protocol();
}

void Mux::Session::select_protocol() {
  if (m_version_selected) return;
  if (m_options.version_f) {
    auto *ctx = pipeline()->context();
    pjs::Value ret;
    (*m_options.version_f)(*ctx, 0, nullptr, ret);
    if (!ctx->ok()) return;
    if (!ret.is_number()) {
      set_pending(true);
      ContextGroup::Waiter::wait(ctx->group());
      MuxBase::Session::input()->input(Data::make());
      return;
    }
    m_version_selected = ret.n();
  } else {
    m_version_selected = m_options.version;
  }

  switch (m_version_selected) {
  case 2:
    upgrade_http2();
    break;
  default:
    if (m_version_selected != 1) {
      Log::error("[muxHTTP] invalid HTTP version: %d", m_version_selected);
      m_version_selected = 1;
    }
    QueueMuxer::chain(Encoder::input());
    Encoder::chain(MuxBase::Session::input());
    MuxBase::Session::chain(Decoder::input());
    Decoder::chain(QueueMuxer::reply());
    Encoder::set_buffer_size(m_options.buffer_size);
    break;
  }

  set_pending(false);
}

void Mux::Session::upgrade_http2() {
  if (!m_http2_muxer) {
    m_http2_muxer = new HTTP2Muxer(m_options);
    m_http2_muxer->open(static_cast<MuxBase::Session*>(this));
  }
}

//
// Mux::SessionCluster
//

auto Mux::SessionCluster::session() -> MuxBase::Session* {
  return new Session(m_options);
}

//
// Server
//

Server::Server(const std::function<Message*(Server*, Message*)> &handler)
  : Decoder(false)
  , Encoder(true)
  , m_handler_func(handler)
  , m_handler(new Handler(this))
{
}

Server::Server(pjs::Object *handler)
  : Decoder(false)
  , Encoder(true)
  , m_handler_obj(handler)
  , m_handler(new Handler(this))
{
}

Server::Server(const Server &r)
  : Filter(r)
  , Decoder(false)
  , Encoder(true)
  , m_handler_func(r.m_handler_func)
  , m_handler_obj(r.m_handler_obj)
  , m_handler(new Handler(this))
{
}

Server::~Server()
{
}

void Server::dump(Dump &d) {
  Filter::dump(d);
  d.name = "serveHTTP";
}

auto Server::clone() -> Filter* {
  return new Server(*this);
}

void Server::chain() {
  Filter::chain();
  Decoder::chain(m_handler->input());
  m_handler->chain(Encoder::input());
  Encoder::chain(Filter::output());
}

void Server::reset() {
  Filter::reset();
  Decoder::reset();
  Encoder::reset();
  m_request_queue.reset();
  m_tunnel = nullptr;
  if (m_http2_server) {
    Decoder::chain(m_handler->input());
    delete m_http2_server;
    m_http2_server = nullptr;
  }
  m_shutdown = false;
}

void Server::process(Event *evt) {
  Filter::output(evt, Decoder::input());
}

void Server::shutdown() {
  Filter::shutdown();
  if (m_http2_server) {
    m_http2_server->go_away();
  } else if (m_request_queue.empty()) {
    Filter::output(StreamEnd::make());
  } else {
    m_shutdown = true;
  }
}

void Server::on_decode_error() {
  Filter::output(StreamEnd::make());
}

void Server::on_decode_request(http::RequestHead *head) {
  auto *req = new RequestQueue::Request;
  req->protocol = head->protocol();
  req->method = head->method();
  req->header_connection = Decoder::header_connection();
  req->header_upgrade = Decoder::header_upgrade();
  m_request_queue.push(req);
  if (req->is_http2()) {
    auto head = ResponseHead::make();
    auto headers = pjs::Object::make();
    head->status(101);
    head->headers(headers);
    headers->set(s_connection, s_upgrade.get());
    headers->set(s_upgrade, s_h2c.get());
    auto inp = Encoder::input();
    inp->input(MessageStart::make(head));
    inp->input(MessageEnd::make());
    upgrade_http2();
    Decoder::chain(m_http2_server->initial_stream());
  }
}

void Server::on_encode_response(pjs::Object *head) {
  if (auto *req = m_request_queue.shift()) {
    Encoder::set_final(req->is_final() || (m_shutdown && m_request_queue.empty()));
    Encoder::set_bodiless(req->is_bodiless());
    Encoder::set_switching(req->is_switching());
    delete req;
  }
}

void Server::on_encode_tunnel() {
  Decoder::set_tunnel(true);
  if (!m_tunnel) {
    if (num_sub_pipelines() > 0) {
      m_tunnel = sub_pipeline(0, false, Filter::output());
    }
  }
}

void Server::on_http2_pass() {
  upgrade_http2();
  m_http2_server->open();
  Decoder::chain(m_http2_server->EventTarget::input());
}

void Server::upgrade_http2() {
  if (!m_http2_server) {
    m_http2_server = new HTTP2Server(this);
    m_http2_server->chain(Filter::output());
  }
}

void Server::on_tunnel_data(Data *data) {
  if (m_tunnel) {
    m_tunnel->input()->input(data);
  }
}

void Server::on_tunnel_end(StreamEnd *end) {
  if (m_tunnel) {
    m_tunnel->input()->input(end);
  }
}

//
// Server::Handler
//

void Server::Handler::on_event(Event *evt) {
  Pipeline::auto_release(this);

  if (m_server->m_tunnel) {
    if (auto data = evt->as<Data>()) {
      m_server->on_tunnel_data(data);
    } else if (auto end = evt->as<StreamEnd>()) {
      m_server->on_tunnel_end(end);
    }

  } else if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
      m_buffer.clear();
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      pjs::Ref<Message> res, req(
        Message::make(
          m_start->head(),
          Data::make(m_buffer)
        )
      );

      m_start = nullptr;
      m_buffer.clear();

      if (auto &func = m_server->m_handler_func) {
        res = func(m_server, req);

      } else if (auto &handler = m_server->m_handler_obj) {
        if (handler->is_instance_of<Message>()) {
          res = handler->as<Message>();

        } else if (handler->is_function()) {
          pjs::Value arg(req), ret;
          if (!m_server->callback(handler->as<pjs::Function>(), 1, &arg, ret)) return;
          if (ret.is_object()) {
            if (auto obj = ret.o()) {
              if (obj->is_instance_of<Message>()) {
                res = obj->as<Message>();
              }
            }
          }
        }
      }

      if (res) {
        output(MessageStart::make(res->head()));
        if (auto *body = res->body()) output(body);
        output(evt);
      } else {
        Log::error("[serveHTTP] handler did not return a valid message");
        output(MessageStart::make());
        output(evt);
      }
    }

  } else if (evt->is<StreamEnd>()) {
    m_server->shutdown();
  }
}

//
// TunnelServer
//

TunnelServer::TunnelServer(pjs::Function *handler)
  : m_handler(handler)
  , m_prop_status(s_status)
{
}

TunnelServer::TunnelServer(const TunnelServer &r)
  : Filter(r)
  , m_handler(r.m_handler)
  , m_prop_status(s_status)
{
}

TunnelServer::~TunnelServer()
{
}

void TunnelServer::dump(Dump &d) {
  Filter::dump(d);
  d.name = "acceptHTTPTunnel";
}

auto TunnelServer::clone() -> Filter* {
  return new TunnelServer(*this);
}

void TunnelServer::reset() {
  Filter::reset();
  m_pipeline = nullptr;
  m_start = nullptr;
  m_buffer.clear();
}

void TunnelServer::process(Event *evt) {
  if (!m_pipeline) {
    if (auto start = evt->as<MessageStart>()) {
      if (!m_start) {
        m_start = start;
      }
    } else if (auto data = evt->as<Data>()) {
      if (m_start) {
        m_buffer.push(*data);
      }
    } else if (evt->is<MessageEnd>()) {
      if (m_start) {
        pjs::Ref<Message> res, req(
          Message::make(
            m_start->head(),
            Data::make(m_buffer)
          )
        );

        m_start = nullptr;
        m_buffer.clear();

        pjs::Value arg(req), ret;
        if (!callback(m_handler, 1, &arg, ret)) return;
        if (ret.is_object()) {
          if (auto obj = ret.o()) {
            if (obj->is_instance_of<Message>()) {
              res = obj->as<Message>();
            }
          }
        }

        if (!res) {
          Log::error("[acceptHTTPTunnel] handler did not return a valid message");
          return;
        }

        if (auto head = res->head()) {
          pjs::Value status;
          m_prop_status.get(head, status);
          if (status.is_undefined() || (status.is_number() && 100 <= status.n() && status.n() < 300)) {
            m_pipeline = sub_pipeline(0, true, output());
          }
        }

        output(MessageStart::make(res->head()));
        if (auto *body = res->body()) output(body);
        output(evt);
        return;
      }
    }
  }

  if (m_pipeline) {
    m_pipeline->input()->input(evt);
  }
}

//
// TunnelClient
//

void TunnelClientReceiver::on_event(Event *evt) {
  static_cast<TunnelClient*>(this)->on_receive(evt);
}

TunnelClient::TunnelClient(const pjs::Value &handshake)
  : m_handshake(handshake)
  , m_prop_status(s_status)
{
}

TunnelClient::TunnelClient(const TunnelClient &r)
  : Filter(r)
  , m_handshake(r.m_handshake)
  , m_prop_status(s_status)
{
}

TunnelClient::~TunnelClient()
{
}

void TunnelClient::dump(Dump &d) {
  Filter::dump(d);
  d.name = "connectHTTPTunnel";
}

auto TunnelClient::clone() -> Filter* {
  return new TunnelClient(*this);
}

void TunnelClient::reset() {
  Filter::reset();
  TunnelClientReceiver::close();
  m_pipeline = nullptr;
  m_status_code = 0;
  m_is_tunnel_started = false;
  m_buffer.clear();
}

void TunnelClient::process(Event *evt) {
  if (!m_pipeline) {
    pjs::Value handshake;
    if (!eval(m_handshake, handshake)) return;
    if (!handshake.is<Message>()) {
      Log::error("[connectHTTPTunnel] invalid handshake request");
      return;
    }
    m_pipeline = sub_pipeline(0, true, TunnelClientReceiver::input());
    auto inp = m_pipeline->input();
    auto msg = handshake.as<Message>();
    inp->input(MessageStart::make(msg->head()));
    if (auto body = msg->body()) inp->input(body);
    inp->input(MessageEnd::make(msg->tail()));
  }

  if (m_is_tunnel_started) {
    m_pipeline->input()->input(evt);
  } else if (auto *data = evt->as<Data>()) {
    m_buffer.push(*data);
  }
}

void TunnelClient::on_receive(Event *evt) {
  if (m_is_tunnel_started || evt->is<StreamEnd>()) {
    output(evt);
  } else if (auto start = evt->as<MessageStart>()) {
    pjs::Value status;
    if (auto *head = start->head()) m_prop_status.get(head, status);
    if (status.is_number()) m_status_code = status.n();
  } else if (evt->is<MessageEnd>()) {
    if (101 <= m_status_code && m_status_code < 300) {
      m_is_tunnel_started = true;
      if (!m_buffer.empty()) {
        m_pipeline->input()->input(Data::make(std::move(m_buffer)));
      }
    }
  }
  if (evt->is<StreamEnd>()) {
    Pipeline::auto_release(m_pipeline);
    m_pipeline = nullptr;
    m_is_tunnel_started = false;
  }
}

} // namespace http
} // namespace pipy
