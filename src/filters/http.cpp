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
#include "api/console.hpp"
#include "compress.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "inbound.hpp"
#include "str-map.hpp"
#include "utils.hpp"

#include <cctype>
#include <queue>
#include <limits>

namespace pipy {
namespace http {

static Data::Producer s_dp("HTTP");

thread_local static const pjs::ConstStr s_protocol("protocol");
thread_local static const pjs::ConstStr s_method("method");
thread_local static const pjs::ConstStr s_GET("GET");
thread_local static const pjs::ConstStr s_HEAD("HEAD");
thread_local static const pjs::ConstStr s_POST("POST");
thread_local static const pjs::ConstStr s_PUT("PUT");
thread_local static const pjs::ConstStr s_PATCH("PATCH");
thread_local static const pjs::ConstStr s_CONNECT("CONNECT");
thread_local static const pjs::ConstStr s_path("path");
thread_local static const pjs::ConstStr s_path_root("/");
thread_local static const pjs::ConstStr s_status("status");
thread_local static const pjs::ConstStr s_status_text("statusText");
thread_local static const pjs::ConstStr s_headers("headers");
thread_local static const pjs::ConstStr s_http_1_0("HTTP/1.0");
thread_local static const pjs::ConstStr s_http_1_1("HTTP/1.1");
thread_local static const pjs::ConstStr s_connection("connection");
thread_local static const pjs::ConstStr s_keep_alive("keep-alive");
thread_local static const pjs::ConstStr s_set_cookie("set-cookie");
thread_local static const pjs::ConstStr s_close("close");
thread_local static const pjs::ConstStr s_transfer_encoding("transfer-encoding");
thread_local static const pjs::ConstStr s_content_length("content-length");
thread_local static const pjs::ConstStr s_content_encoding("content-encoding");
thread_local static const pjs::ConstStr s_upgrade("upgrade");
thread_local static const pjs::ConstStr s_websocket("websocket");
thread_local static const pjs::ConstStr s_h2c("h2c");
thread_local static const pjs::ConstStr s_bad_gateway("Bad Gateway");
thread_local static const pjs::ConstStr s_cannot_resolve("Cannot Resolve");
thread_local static const pjs::ConstStr s_connection_refused("Connection Refused");
thread_local static const pjs::ConstStr s_unauthorized("Unauthorized");
thread_local static const pjs::ConstStr s_read_error("Read Error");
thread_local static const pjs::ConstStr s_write_error("Write Error");
thread_local static const pjs::ConstStr s_gateway_timeout("Gateway Timeout");
thread_local static const pjs::ConstStr s_http2_preface_method("PRI");
thread_local static const pjs::ConstStr s_http2_preface_path("*");
thread_local static const pjs::ConstStr s_http2_preface_protocol("HTTP/2.0");

thread_local static const StrMap s_strmap_methods({
  "PRI", "GET", "HEAD", "POST", "PUT",
  "PATCH", "DELETE", "CONNECT", "OPTIONS", "TRACE",
});

thread_local static const StrMap s_strmap_paths({
  "*", "/", "/index.html",
});

thread_local static const StrMap s_strmap_protocols({
  "HTTP/1.0",
  "HTTP/1.1",
  "HTTP/2.0",
});

thread_local static const StrMap s_strmap_statuses({
  "OK", "Created", "Continue",
});


thread_local static const StrMap s_strmap_headers({
  "host",
  "user-agent",
  "accept",
  "connection",
  "content-length",
  "content-type",
  "transfer-encoding",
});

thread_local static const StrMap s_strmap_header_values({
  "*/*",
  "text/html",
  "application/json",
  "chunked",
  "close",
  "keep-alive",
});

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

static auto read_str(Data::Reader &dr, char ending, const StrMap &strmap) -> pjs::Str* {
  size_t i = 0;
  StrMap::Parser p(strmap);
  pjs::Str *found = nullptr;
  for (;;) {
    auto c = dr.get();
    if (c < 0) return nullptr;
    if (c == ending) return found;
    if (c == ' ' && !i) continue;
    found = p.parse(c);
    if (found == pjs::Str::empty) return nullptr;
    i++;
  }
}

static auto read_str(Data::Reader &dr, char ending, const StrMap &strmap, char *buf) -> pjs::Str* {
  size_t i = 0;
  StrMap::Parser p(strmap);
  pjs::Str *found = nullptr;
  for (;;) {
    auto c = dr.get();
    if (c < 0) return nullptr;
    if (c == ending) {
      if (found && found != pjs::Str::empty) {
        return found;
      } else {
        return i > 0 ? pjs::Str::make(buf, i) : pjs::Str::empty.get();
      }
    }
    if (c == ' ' && !i) continue;
    found = p.parse(c);
    buf[i++] = c;
  }
}

static auto read_str_lower(Data::Reader &dr, char ending, const StrMap &strmap, char *buf) -> pjs::Str* {
  size_t i = 0;
  StrMap::Parser p(strmap);
  pjs::Str *found = nullptr;
  for (;;) {
    auto c = dr.get();
    if (c < 0) return nullptr;
    if (c == ending) {
      if (found && found != pjs::Str::empty) {
        return found;
      } else {
        return i > 0 ? pjs::Str::make(buf, i) : nullptr;
      }
    }
    if (c == ' ' && !i) continue;
    c = std::tolower(c);
    found = p.parse(c);
    buf[i++] = c;
  }
}

static auto read_uint(Data::Reader &dr, char ending) -> int {
  int n = 0;
  for (;;) {
    auto c = dr.get();
    if (c < 0) return -1;
    if (c == ending) return n;
    if ('0' <= c && c <= '9') {
      n = n * 10 + (c - '0');
    } else {
      return -1;
    }
  }
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
  m_responded_tunnel_type = TunnelType::NONE;
  m_body_size = 0;
  m_is_bodiless = false;
  m_is_tunnel = false;
  m_has_error = false;
}

void Decoder::on_event(Event *evt) {
  if (m_is_tunnel) {
    EventFunction::output(evt);
    return;
  }

  if (auto e = evt->as<StreamEnd>()) {
    stream_end(e);
    reset();
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
        Data::Reader dr(m_head_buffer);
        char buf[m_head_buffer.size()];
        if (m_is_response) {
          pjs::Ref<pjs::Str> protocol, status_text; int status;
          protocol = read_str(dr, ' ', s_strmap_protocols); if (!protocol) { error(); break; }
          status = read_uint(dr, ' '); if (status < 100 || status > 599) { error(); break; }
          status_text = read_str(dr, '\r', s_strmap_statuses, buf); if (!status_text) { error(); break; }
          auto res = ResponseHead::make();
          res->protocol = protocol;
          res->status = status;
          res->statusText = status_text;
          m_head = res;
        } else {
          pjs::Ref<pjs::Str> method, path, protocol;
          method = read_str(dr, ' ', s_strmap_methods); if (!method) { error(); break; }
          path = read_str(dr, ' ', s_strmap_paths, buf); if (!path) { error(); break; }
          protocol = read_str(dr, '\r', s_strmap_protocols); if (!protocol) { error(); break; }
          if (
            (s_http2_preface_method == method) &&
            (s_http2_preface_path == path) &&
            (s_http2_preface_protocol == protocol)
          ) {
            m_body_size = 8;
            state = HTTP2_PREFACE;
            break;
          } else if (protocol != s_http_1_0 && protocol != s_http_1_1) {
            error();
            break;
          } else {
            auto req = RequestHead::make();
            req->method = method;
            req->path = path;
            req->protocol = protocol;
            m_head = req;
          }
        }
        m_head->headers = pjs::Object::make();
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
          Data::Reader dr(m_head_buffer);
          char buf[len];
          pjs::Ref<pjs::Str> key(read_str_lower(dr, ':', s_strmap_headers, buf));
          pjs::Ref<pjs::Str> val(read_str(dr, '\r', s_strmap_header_values, buf));
          if (!key || !val) { error(); break; }
          auto headers = m_head->headers.get();
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
            headers->set(key, v);
            if (key == s_transfer_encoding) m_header_transfer_encoding = v;
            else if (key == s_content_length) m_header_content_length = v;
            else if (key == s_connection) m_header_connection = v;
            else if (key == s_upgrade) m_header_upgrade = v;
          }
          state = HEADER;
          m_head_buffer.clear();

        } else {
          m_body_size = 0;
          m_head_buffer.clear();

          static const std::string s_chunked("chunked");

          // Transfer-Encoding and Content-Length
          if (
            m_header_transfer_encoding &&
            utils::starts_with(m_header_transfer_encoding->str(), s_chunked)
          ) {
            message_start();
            if (m_is_bodiless) {
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
              auto status = m_head->as<ResponseHead>()->status;
              if (status >= 200 && status != 204 && status != 304) {
                m_body_size = std::numeric_limits<int>::max();
              }
            }
            if (m_body_size > 0) {
              if (m_is_bodiless) {
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
        if (on_decode_tunnel(TunnelType::HTTP2)) {
          m_is_tunnel = true;
        }
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
    m_is_bodiless = false;
    auto res = m_head->as<ResponseHead>();
    if (auto req = on_decode_response(res)) {
      m_is_bodiless = req->is_bodiless;
      auto tt = req->tunnel_type;
      if (res->is_tunnel(tt)) m_responded_tunnel_type = tt;
      delete req;
    }
  } else {
    auto head = m_head->as<RequestHead>();
    auto req = new RequestQueue::Request;
    req->head = head;
    req->is_final = head->is_final(m_header_connection);
    req->is_bodiless = head->is_bodiless();
    req->tunnel_type = head->tunnel_type(m_header_upgrade);
    on_decode_request(req);
  }
  output(MessageStart::make(m_head));
}

void Decoder::message_end() {
  if (m_responded_tunnel_type != TunnelType::NONE) {
    if (on_decode_tunnel(m_responded_tunnel_type)) {
      m_is_tunnel = true;
    }
  }
  output(MessageEnd::make());
}

void Decoder::stream_end(StreamEnd *end) {
  if (m_is_response && (m_state == HEAD || m_state == HEADER) && end->has_error()) {
    int status_code = 0;
    pjs::Str *status_text = nullptr;
    switch (end->error_code()) {
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
    head->headers = pjs::Object::make();
    head->protocol = s_http_1_1;
    head->status = status_code;
    head->statusText = status_text;
    output(MessageStart::make(head));
    if (!end->error().is_undefined()) {
      Data buf;
      Data::Builder db(buf, &s_dp);
      Console::dump(end->error(), db);
      db.flush();
      output(Data::make(std::move(buf)));
    }
    output(end);
  } else {
    output(end);
  }
}

//
// Encoder
//

Encoder::Encoder(bool is_response)
  : m_is_response(is_response)
{
}

void Encoder::reset() {
  m_buffer.clear();
  m_head = nullptr;
  m_protocol = nullptr;
  m_method = nullptr;
  m_path = nullptr;
  m_header_connection = nullptr;
  m_header_upgrade = nullptr;
  m_responded_tunnel_type = TunnelType::NONE;
  m_status_code = 0;
  m_content_length = 0;
  m_chunked = false;
  m_is_final = false;
  m_is_bodiless = false;
  m_is_tunnel = false;
}

void Encoder::on_event(Event *evt) {
  if (m_is_tunnel) {
    output(evt);
    return;
  }

  if (auto start = evt->as<MessageStart>()) {
    if (!m_head) {
      m_content_length = 0;
      m_chunked = false;
      m_buffer.clear();

      if (m_is_response) {
        auto head = pjs::coerce<ResponseHead>(start->head());
        auto protocol = head->protocol.get();
        if (!protocol || !protocol->length()) protocol = s_http_1_1;
        m_head = head;
        m_protocol = protocol;
        m_status_code = head->status;
        m_is_final = false;
        m_is_bodiless = false;
        if (auto req = on_encode_response(head)) {
          m_is_final = req->is_final;
          m_is_bodiless = req->is_bodiless;
          auto tt = req->tunnel_type;
          if (head->is_tunnel(tt)) m_responded_tunnel_type = tt;
          delete req;
        }

      } else {
        auto head = pjs::coerce<RequestHead>(start->head());
        auto protocol = head->protocol.get();
        auto method = head->method.get();
        auto path = head->path.get();
        if (!protocol || !protocol->length()) protocol = s_http_1_1;
        if (!method || !method->length()) method = s_GET;
        if (!path || !path->length()) path = s_path_root;
        m_head = head;
        m_protocol = protocol;
        m_method = method;
        m_path = path;
      }
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_head) {
      if (m_is_bodiless) {
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
    if (m_head) {
      if (m_is_response && m_is_final) {
        output_end(StreamEnd::make());
      } else {
        output_end(evt);
      }
    }

    m_buffer.clear();
    m_head = nullptr;

  } else if (evt->is<StreamEnd>()) {
    output(evt);
    m_buffer.clear();
    m_head = nullptr;
  }
}

void Encoder::output_head() {
  auto buffer = Data::make();
  bool no_content_length = false;

  Data::Builder db(*buffer, &s_dp);

  if (m_is_response) {
    auto head = m_head->as<ResponseHead>();
    char str[100];
    auto len = utils::to_string(str, sizeof(str), m_status_code);
    db.push(m_protocol->str());
    db.push(' ');
    db.push(str, len);
    db.push(' ');
    if (auto s = head->statusText.get()) {
      db.push(s->str());
      db.push("\r\n");
    } else {
      if (auto str = lookup_status_text(m_status_code)) {
        db.push(str);
        db.push("\r\n");
      } else {
        db.push("OK\r\n");
      }
    }

    auto status = m_status_code;
    if (
      (status < 200 || status == 204) ||
      (m_responded_tunnel_type != TunnelType::NONE)
    ) {
      no_content_length = true;
    }

  } else {
    db.push(m_method->str());
    db.push(' ');
    db.push(m_path->str());
    db.push(' ');
    db.push(m_protocol->str());
    db.push("\r\n");
  }

  if (auto headers = m_head->headers.get()) {
    headers->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        if (k == s_keep_alive) return;
        if (k == s_transfer_encoding) return;
        if (k == s_content_length) {
          if (m_is_bodiless) {
            no_content_length = true;
          } else {
            return;
          }
        } else if (k == s_connection) {
          if (v.is_string()) {
            m_header_connection = v.s();
            if (!utils::iequals(v.s()->str(), s_upgrade.get()->str())) {
              return;
            }
          }
        } else if (k == s_upgrade) {
          if (v.is_string()) m_header_upgrade = v.s();
        }
        if (k == s_set_cookie && v.is_array()) {
          v.as<pjs::Array>()->iterate_all(
            [&](pjs::Value &v, int) {
              auto s = v.to_string();
              db.push(k->str());
              db.push(": ");
              db.push(s->str());
              db.push("\r\n");
              s->release();
            }
          );
        } else {
          db.push(k->str());
          db.push(": ");
          auto s = v.to_string();
          db.push(s->str());
          db.push("\r\n");
          s->release();
        }
      }
    );
  }

  if (!m_is_response) {
    auto head = m_head->as<RequestHead>();
    auto req = new RequestQueue::Request;
    req->head = head;
    req->is_final = head->is_final(m_header_connection);
    req->is_bodiless = head->is_bodiless();
    req->tunnel_type = head->tunnel_type(m_header_upgrade);
    on_encode_request(req);
  }

  if (!no_content_length) {
    if (m_chunked) {
      static const std::string str("transfer-encoding: chunked\r\n");
      db.push(str);
    } else if (
      m_content_length > 0 ||
      m_is_response ||
      m_method == s_POST ||
      m_method == s_PUT ||
      m_method == s_PATCH
    ) {
      char str[100];
      auto len = utils::to_string(str, sizeof(str), m_content_length);
      db.push(s_content_length.get()->str());
      db.push(": ", 2);
      db.push(str, len);
      db.push("\r\n", 2);
    }
  }

  if (m_is_final) {
    static const std::string str("connection: close\r\n");
    db.push(str);
  } else {
    static const std::string str("connection: keep-alive\r\n");
    db.push(str);
  }

  db.push("\r\n");
  db.flush();

  output(MessageStart::make(m_head));
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
  if (m_is_bodiless) {
    output_head();
  } else if (m_chunked) {
    output(s_dp.make("0\r\n\r\n"));
  } else {
    output_head();
    if (!m_buffer.empty()) {
      output(Data::make(std::move(m_buffer)));
    }
  }
  if (m_responded_tunnel_type != TunnelType::NONE) {
    if (on_encode_tunnel(m_responded_tunnel_type)) {
      m_is_tunnel = true;
    }
  }
  output(evt);
  m_header_connection = nullptr;
  m_header_upgrade = nullptr;
}

//
// RequestDecoder
//

RequestDecoder::RequestDecoder(pjs::Function *handler)
  : Decoder(false)
  , m_handler(handler)
{
}

RequestDecoder::RequestDecoder(const RequestDecoder &r)
  : Filter(r)
  , Decoder(false)
  , m_handler(r.m_handler)
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
  Decoder::chain(Filter::output());
}

void RequestDecoder::reset() {
  Filter::reset();
  Decoder::reset();
}

void RequestDecoder::process(Event *evt) {
  if (evt->is<Data>()) {
    Decoder::input()->input(evt);
  } else if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  }
}

void RequestDecoder::on_decode_request(RequestQueue::Request *req) {
  if (m_handler) {
    pjs::Value arg(req->head), ret;
    Filter::callback(m_handler, 1, &arg, ret);
  }
  delete req;
}

//
// ResponseDecoder
//

ResponseDecoder::ResponseDecoder(pjs::Function *handler)
  : Decoder(true)
  , m_handler(handler)
{
}

ResponseDecoder::ResponseDecoder(const ResponseDecoder &r)
  : Filter(r)
  , Decoder(true)
  , m_handler(r.m_handler)
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

auto ResponseDecoder::on_decode_response(ResponseHead *head) -> RequestQueue::Request* {
  if (!m_handler) return nullptr;
  pjs::Value arg(head), ret;
  if (Filter::callback(m_handler, 1, &arg, ret)) {
    if (ret.is_nullish()) return nullptr;
    if (ret.is_object()) {
      auto req = new RequestQueue::Request;
      req->head = pjs::coerce<RequestHead>(ret.o());
      return req;
    }
    Filter::error("callback did not return an object for request head");
  }
  return nullptr;
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

RequestEncoder::RequestEncoder(const Options &options, pjs::Function *handler)
  : Encoder(false)
  , m_options(options)
  , m_handler(handler)
{
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : Filter(r)
  , Encoder(false)
  , m_options(r.m_options)
  , m_handler(r.m_handler)
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
    Encoder::input()->input(evt);
  }
}

void RequestEncoder::on_encode_request(RequestQueue::Request *req) {
  if (m_handler) {
    pjs::Value arg(req->head), ret;
    Filter::callback(m_handler, 1, &arg, ret);
  }
  delete req;
}

//
// ResponseEncoder
//

ResponseEncoder::Options::Options(pjs::Object *options) {
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
}

//
// ResponseEncoder
//

ResponseEncoder::ResponseEncoder(const Options &options, pjs::Function *handler)
  : Encoder(true)
  , m_options(options)
  , m_handler(handler)
{
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : Filter(r)
  , Encoder(true)
  , m_options(r.m_options)
  , m_handler(r.m_handler)
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
    Encoder::input()->input(evt);
  }
}

auto ResponseEncoder::on_encode_response(ResponseHead *head) -> RequestQueue::Request* {
  if (!m_handler) return nullptr;
  pjs::Value arg(head), ret;
  if (Filter::callback(m_handler, 1, &arg, ret)) {
    if (ret.is_nullish()) return nullptr;
    if (ret.is_object()) {
      auto req = new RequestQueue::Request;
      req->head = pjs::coerce<RequestHead>(ret.o());
      return req;
    }
    Filter::error("callback did not return an object for request head");
  }
  return nullptr;
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
{
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , Decoder(false)
  , Encoder(true)
  , m_options(r.m_options)
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
  Decoder::chain(Demuxer::Queue::input());
  Demuxer::Queue::chain(Encoder::input());
  Encoder::chain(Filter::output());
  Encoder::set_buffer_size(m_options.buffer_size);
}

void Demux::reset() {
  Filter::reset();
  Decoder::reset();
  Encoder::reset();
  Demuxer::reset();
  Demuxer::Queue::reset();
  m_request_queue.reset();
  if (m_http2_demuxer) {
    Decoder::chain(Demuxer::Queue::input());
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
  } else {
    m_shutdown = true;
  }
}

auto Demux::on_queue_message(MessageStart *start) -> int {
  auto req = m_request_queue.head();
  return req->tunnel_type == TunnelType::NONE ? 1 : -1;
}

auto Demux::on_open_stream() -> EventFunction* {
  return Demuxer::open_stream(
    Filter::sub_pipeline(0, true)
  );
}

void Demux::on_decode_error() {
  Filter::output(StreamEnd::make());
}

void Demux::on_decode_request(RequestQueue::Request *req) {
  m_request_queue.push(req);
  if (req->tunnel_type == TunnelType::HTTP2) {
    upgrade_http2();
    Decoder::chain(m_http2_demuxer->initial_stream());
    auto head = ResponseHead::make();
    auto headers = pjs::Object::make();
    head->status = 101;
    head->headers = headers;
    headers->set(s_connection, s_upgrade.get());
    headers->set(s_upgrade, s_h2c.get());
    auto out = Encoder::input();
    out->input(MessageStart::make(head));
    out->input(MessageEnd::make());
  }
}

auto Demux::on_encode_response(ResponseHead *head) -> RequestQueue::Request* {
  if (head->status == 100) {
    Demuxer::Queue::increase_output_count();
    return nullptr;
  } else {
    auto req = m_request_queue.shift();
    if (m_shutdown && m_request_queue.empty()) req->is_final = true;
    return req;
  }
}

bool Demux::on_decode_tunnel(TunnelType tt) {
  if (tt == TunnelType::HTTP2) {
    upgrade_http2();
    m_http2_demuxer->init();
    Decoder::chain(m_http2_demuxer->EventTarget::input());
    return true;
  } else {
    return false;
  }
}

bool Demux::on_encode_tunnel(TunnelType tt) {
  if (tt == TunnelType::HTTP2) {
    return true;
  } else {
    Decoder::set_tunnel();
    Demuxer::Queue::dedicate();
  }
  return true;
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
  : Muxer::Options(options)
  , http2::Endpoint::Options(options)
{
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
  Value(options, "version")
    .get(version)
    .get(version_s)
    .get(version_f)
    .check_nullable();
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

Mux::Mux(pjs::Function *session_selector, const Options &options)
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
  if (options) {
    try {
      Options opts(options);
      return new SessionCluster(opts);
    } catch (std::runtime_error &err) {
      Filter::error(err.what());
      return nullptr;
    }
  } else {
    return new SessionCluster(m_options);
  }
}

auto Mux::verify_http_version(int version) -> int {
  if (version == 1 || version == 2) return version;
  Filter::error("invalid HTTP version number");
  return 0;
}

auto Mux::verify_http_version(pjs::Str *name) -> int {
  thread_local static const pjs::ConstStr s_http_1("http/1");
  thread_local static const pjs::ConstStr s_http_2("http/2");
  thread_local static const pjs::ConstStr s_http_1_0("http/1.0");
  thread_local static const pjs::ConstStr s_http_1_1("http/1.1");
  thread_local static const pjs::ConstStr s_h2("h2");
  if (name == s_http_2 || name == s_h2) return 2;
  if (name == s_http_1 || name == s_http_1_0 || name == s_http_1_1) return 1;
  Filter::error("invalid HTTP version name");
  return 0;
}

//
// Mux::Session
//

Mux::Session::~Session() {
  if (m_version_selector) m_version_selector->close();
  delete m_http2_muxer;
}

void Mux::Session::open(Muxer *muxer) {
  if (!select_protocol(muxer)) {
    set_pending(true);
    pipeline()->input()->flush();
  }
}

auto Mux::Session::open_stream(Muxer *muxer) -> EventFunction* {
  if (m_http2_muxer) {
    return m_http2_muxer->stream();
  } else {
    return Muxer::Queue::open_stream(muxer);
  }
}

void Mux::Session::close_stream(EventFunction *stream) {
  if (m_http2_muxer) {
    m_http2_muxer->close(stream);
  } else {
    Muxer::Queue::close_stream(stream);
  }
}

void Mux::Session::close() {
  Muxer::Queue::reset();
  m_request_queue.reset();
  if (m_http2_muxer) {
    InputContext ic;
    m_http2_muxer->go_away();
  }
}

void Mux::Session::on_encode_request(RequestQueue::Request *req) {
  m_request_queue.push(req);
}

auto Mux::Session::on_decode_response(ResponseHead *head) -> RequestQueue::Request* {
  if (head->status == 100) {
    Muxer::Queue::increase_queue_count();
    return nullptr;
  } else {
    return m_request_queue.shift();
  }
}

bool Mux::Session::on_decode_tunnel(TunnelType tt) {
  Encoder::set_tunnel();
  Muxer::Queue::dedicate();
  return true;
}

void Mux::Session::on_decode_error()
{
}

bool Mux::Session::select_protocol(Muxer *muxer) {
  thread_local static auto s_method_select = pjs::ClassDef<VersionSelector>::method("select");

  if (m_version_selected) return true;

  auto mux = static_cast<Mux*>(muxer);
  if (m_options.version_f) {
    pjs::Value ret;
    if (!mux->eval(m_options.version_f, ret)) return false;
    if (ret.is<pjs::Promise>()) {
      m_version_selector = VersionSelector::make(mux, this);
      ret.as<pjs::Promise>()->then(mux->context(), pjs::Function::make(s_method_select, m_version_selector));
      return false;
    }
    return select_protocol(muxer, ret);
  } else if (m_options.version_s) {
    return select_protocol(muxer, m_options.version_s.get());
  } else {
    return select_protocol(muxer, m_options.version);
  }
}

bool Mux::Session::select_protocol(Muxer *muxer, const pjs::Value &version) {
  auto mux = static_cast<Mux*>(muxer);
  if (version.is_number()) {
    m_version_selected = mux->verify_http_version(version.to_int32());
  } else if (version.is_string()) {
    m_version_selected = mux->verify_http_version(version.s());
  } else {
    mux->error("invalid HTTP version");
  }

  switch (m_version_selected) {
  case 1:
    Muxer::Queue::chain(Encoder::input());
    Encoder::chain(MuxBase::Session::input());
    MuxBase::Session::chain(Decoder::input());
    Decoder::chain(Muxer::Queue::reply());
    Encoder::set_buffer_size(m_options.buffer_size);
    MuxBase::Session::set_pending(false);
    return true;
  case 2:
    upgrade_http2();
    MuxBase::Session::set_pending(false);
    return true;
  default:
    break;
  }

  return false;
}

void Mux::Session::upgrade_http2() {
  if (!m_http2_muxer) {
    m_http2_muxer = new HTTP2Muxer(m_options);
    m_http2_muxer->open(static_cast<MuxBase::Session*>(this));
  }
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
  m_handler->reset();
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

void Server::on_decode_request(RequestQueue::Request *req) {
  m_request_queue.push(req);
  if (req->tunnel_type == TunnelType::HTTP2) {
    upgrade_http2();
    Decoder::chain(m_http2_server->initial_stream());
    auto head = ResponseHead::make();
    auto headers = pjs::Object::make();
    head->status = 101;
    head->headers = headers;
    headers->set(s_connection, s_upgrade.get());
    headers->set(s_upgrade, s_h2c.get());
    auto out = Encoder::input();
    out->input(MessageStart::make(head));
    out->input(MessageEnd::make());
  }
}

auto Server::on_encode_response(ResponseHead *head) -> RequestQueue::Request* {
  return m_request_queue.shift();
}

bool Server::on_decode_tunnel(TunnelType tt) {
  if (tt == TunnelType::HTTP2) {
    upgrade_http2();
    m_http2_server->init();
    Decoder::chain(m_http2_server->EventTarget::input());
    return true;
  } else {
    return false;
  }
}

bool Server::on_encode_tunnel(TunnelType tt) {
  if (tt == TunnelType::HTTP2) {
    return true;
  } else if (num_sub_pipelines() > 0) {
    Decoder::set_tunnel();
    if (!m_tunnel) {
      m_tunnel = sub_pipeline(0, false, Filter::output())->start();
    }
    return true;
  } else {
    return false;
  }
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

void Server::Handler::reset() {
  m_message_reader.reset();
}

void Server::Handler::on_event(Event *evt) {
  Pipeline::auto_release(this);

  if (m_server->m_tunnel) {
    if (auto data = evt->as<Data>()) {
      m_server->on_tunnel_data(data);
    } else if (auto end = evt->as<StreamEnd>()) {
      m_server->on_tunnel_end(end);
    }
    return;
  }

  if (evt->is<StreamEnd>()) {
    m_server->shutdown();
    return;
  }

  if (auto req = m_message_reader.read(evt)) {
    pjs::Ref<Message> res;

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

    req->release();

    if (res) {
      res->write(EventFunction::output());
    } else {
      m_server->Filter::error("handler is not or did not return a Message");
    }
  }
}

//
// TunnelServer
//

TunnelServer::TunnelServer(pjs::Function *handler)
  : m_handler(handler)
{
}

TunnelServer::TunnelServer(const TunnelServer &r)
  : Filter(r)
  , m_handler(r.m_handler)
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
  m_message_reader.reset();
}

void TunnelServer::process(Event *evt) {
  if (m_pipeline) {
    m_pipeline->input()->input(evt);

  } else if (auto req = m_message_reader.read(evt)) {
    pjs::Value arg(req), ret;
    req->release();
    if (!callback(m_handler, 1, &arg, ret)) return;

    pjs::Ref<Message> res;
    if (ret.is_nullish()) {
      res = Message::make();
    } else if (ret.is_instance_of<Message>()) {
      res = ret.as<Message>();
    } else {
      Filter::error("handler did not return a Message");
      return;
    }

    pjs::Ref<RequestHead> req_head = pjs::coerce<RequestHead>(req->head());
    pjs::Ref<ResponseHead> res_head = pjs::coerce<ResponseHead>(res->head());
    if (res_head->is_tunnel(req_head->tunnel_type())) {
      m_pipeline = sub_pipeline(0, true, output())->start();
    }

    res->write(Filter::output());
  }
}

//
// TunnelClient
//

TunnelClient::TunnelClient(pjs::Object *handshake)
  : m_handshake(handshake)
{
}

TunnelClient::TunnelClient(const TunnelClient &r)
  : Filter(r)
  , m_handshake(r.m_handshake)
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
  m_buffer.clear();
  m_pipeline = nullptr;
  m_request_head = nullptr;
  m_response_head = nullptr;
  m_is_tunnel_started = false;
}

void TunnelClient::process(Event *evt) {
  if (!m_pipeline) {
    pjs::Ref<pjs::Object> handshake;
    if (m_handshake) {
      if (m_handshake->is_instance_of<Message>()) {
        handshake = m_handshake;
      } else if (m_handshake->is_function()) {
        pjs::Value ret;
        if (!eval(m_handshake->as<pjs::Function>(), ret)) return;
        if (ret.is_instance_of<Message>()) handshake = ret.o();
      }
    }
    if (!handshake) {
      Filter::error("handshake is not or did not return a request Message");
      return;
    }
    auto msg = handshake->as<Message>();
    m_request_head = pjs::coerce<RequestHead>(msg->head());
    m_pipeline = sub_pipeline(0, true, EventSource::reply())->start();
    msg->as<Message>()->write(m_pipeline->input());
  }

  if (m_is_tunnel_started) {
    if (!m_buffer.empty()) {
      m_pipeline->input()->input(Data::make(std::move(m_buffer)));
    }
    m_pipeline->input()->input(evt);
  } else if (auto *data = evt->as<Data>()) {
    m_buffer.push(*data);
  }
}

void TunnelClient::on_reply(Event *evt) {
  if (m_is_tunnel_started || evt->is<StreamEnd>()) {
    Filter::output(evt);

  } else if (auto start = evt->as<MessageStart>()) {
    if (!m_response_head) {
      m_response_head = pjs::coerce<ResponseHead>(start->head());
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_response_head) {
      auto tt = m_request_head->tunnel_type();
      if (m_response_head->is_tunnel(tt)) {
        m_is_tunnel_started = true;
        EventFunction::input()->flush_async();
      } else {
        Filter::output(StreamEnd::make());
      }
      m_request_head = nullptr;
      m_response_head = nullptr;
    }
  }
}

} // namespace http
} // namespace pipy

namespace pjs {

using namespace pipy::http;

template<> void ClassDef<Mux::Session::VersionSelector>::init() {
  method("select", [](Context &ctx, Object *obj, Value &) {
    Value version; ctx.get(0, version);
    obj->as<Mux::Session::VersionSelector>()->select(version);
  });
}

} // namespace pjs
