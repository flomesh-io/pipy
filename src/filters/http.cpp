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
thread_local static const pjs::ConstStr s_cookie("cookie");
thread_local static const pjs::ConstStr s_set_cookie("set-cookie");
thread_local static const pjs::ConstStr s_close("close");
thread_local static const pjs::ConstStr s_transfer_encoding("transfer-encoding");
thread_local static const pjs::ConstStr s_content_length("content-length");
thread_local static const pjs::ConstStr s_content_encoding("content-encoding");
thread_local static const pjs::ConstStr s_upgrade("upgrade");
thread_local static const pjs::ConstStr s_websocket("websocket");
thread_local static const pjs::ConstStr s_h2c("h2c");
thread_local static const pjs::ConstStr s_http2_preface_method("PRI");
thread_local static const pjs::ConstStr s_http2_preface_path("*");
thread_local static const pjs::ConstStr s_http2_preface_protocol("HTTP/2.0");
thread_local static const pjs::ConstStr s_http2_settings("http2-settings");

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

static auto read_str_lower(Data::Reader &dr, char ending, const StrMap &strmap, char *buf, char *buf_lower) -> pjs::Str* {
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
        return i > 0 ? pjs::Str::make(buf_lower, i) : nullptr;
      }
    }
    if (c == ' ' && !i) continue;
    auto l = std::tolower(c);
    found = p.parse(l);
    buf_lower[i] = l;
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
  m_method = nullptr;
  m_header_transfer_encoding = nullptr;
  m_header_content_length = nullptr;
  m_header_connection = nullptr;
  m_header_upgrade = nullptr;
  m_responded_tunnel_type = TunnelType::NONE;
  m_current_size = 0;
  m_head_size = 0;
  m_body_size = 0;
  m_is_tunnel = false;
  m_has_error = false;
}

void Decoder::on_event(Event *evt) {
  if (m_is_tunnel) {
    EventFunction::output(evt);
    return;
  }

  if (auto eos = evt->as<StreamEnd>()) {
    stream_end(eos);
    reset();
    return;
  }

  auto data = evt->as<Data>();
  if (!data) return;

  while (!m_has_error && !data->empty()) {
    auto state = m_state;
    Data output;

    // fast scan over the body
    if (state == BODY || state == CHUNK_BODY) {
      auto n = std::min(m_current_size, data->size());
      data->shift(n, output);
      if (0 == (m_current_size -= n)) state = (state == BODY ? HEAD : CHUNK_TAIL);

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
            m_body_size++;
            if (c == '\n') {
              if (m_current_size > 0) {
                state = CHUNK_BODY;
                return true;
              } else {
                state = CHUNK_LAST;
                return false;
              }
            }
            else if ('0' <= c && c <= '9') m_current_size = (m_current_size << 4) + (c - '0');
            else if ('a' <= c && c <= 'f') m_current_size = (m_current_size << 4) + (c - 'a') + 10;
            else if ('A' <= c && c <= 'F') m_current_size = (m_current_size << 4) + (c - 'A') + 10;
            return false;

          case CHUNK_TAIL:
            m_body_size++;
            if (c == '\n') {
              state = CHUNK_HEAD;
              m_current_size = 0;
            }
            return false;

          case CHUNK_LAST:
            m_body_size++;
            if (c == '\n') {
              state = HEAD;
              return true;
            }
            return false;

          case HEAD_EOL:
          case HEADER_EOL:
            return false;

          case HTTP2_PREFACE:
            if (!--m_current_size) {
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
        output
      );
    }

    // old state
    switch (m_state) {
      case HEAD:
      case HEADER:
        if (m_head_buffer.size() + output.size() <= m_max_header_size) {
          m_head_buffer.push(output);
        } else {
          Log::error("HTTP header size overflow");
          error();
        }
        break;
      case BODY:
      case CHUNK_BODY:
        m_body_size += output.size();
        EventFunction::output(Data::make(std::move(output)));
        break;
      default: break;
    }

    if (m_has_error) break;

    // new state
    switch (state) {
      case HEAD_EOL: {
        Data::Reader dr(m_head_buffer);
        auto len = m_head_buffer.size();
        pjs::vl_array<char, DATA_CHUNK_SIZE> buf(len);
        m_head_size += len;
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
            m_current_size = 8;
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
        m_head->headerNames = pjs::Object::make();
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
        pjs::vl_array<char, DATA_CHUNK_SIZE> buf(len);
        pjs::vl_array<char, DATA_CHUNK_SIZE> buf_lower(len);
        m_head_size += len;
        if (len > 2) {
          Data::Reader dr(m_head_buffer);
          pjs::Ref<pjs::Str> key(read_str_lower(dr, ':', s_strmap_headers, buf, buf_lower));
          pjs::Ref<pjs::Str> val(read_str(dr, '\r', s_strmap_header_values, buf_lower));
          if (!key || !val) { error(); break; }
          auto headers = m_head->headers.get();
          if (key == s_cookie || key == s_set_cookie) {
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
            if (key == s_transfer_encoding) m_header_transfer_encoding = v;
            else if (key == s_content_length) m_header_content_length = v;
            else if (key == s_connection) { m_header_connection = v; v = nullptr; }
            else if (key == s_upgrade) m_header_upgrade = v;
            if (v) headers->set(key, v);
          }
          if (auto names = m_head->headerNames.get()) {
            pjs::Ref<pjs::Str> name(pjs::Str::make(buf, key->size()));
            if (name != key) {
              names->set(key, name.get());
            }
          }
          state = HEADER;
          m_head_buffer.clear();

        } else {
          m_current_size = 0;
          m_head_buffer.clear();

          static const std::string s_chunked("chunked");

          // Transfer-Encoding and Content-Length
          if (
            m_header_transfer_encoding &&
            utils::starts_with(m_header_transfer_encoding->str(), s_chunked)
          ) {
            message_start();
            if (m_method == s_HEAD) {
              message_end();
              state = HEAD;
            } else {
              state = CHUNK_HEAD;
            }
          } else {
            message_start();
            if (m_header_content_length) {
              m_current_size = std::atoi(m_header_content_length->c_str());
            } else if (m_is_response && m_method != s_HEAD && m_method != s_CONNECT) {
              auto status = m_head->as<ResponseHead>()->status;
              if (status >= 200 && status != 204 && status != 304) {
                m_current_size = std::numeric_limits<int>::max();
              }
            }
            if (m_current_size > 0) {
              if (m_method == s_HEAD) {
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
      if (!data->empty()) {
        EventFunction::output(Data::make(std::move(*data)));
      }
    }

    m_state = state;
  }
}

void Decoder::message_start() {
  if (m_is_response) {
    m_method = nullptr;
    m_responded_tunnel_type = TunnelType::NONE;
    auto res = m_head->as<ResponseHead>();
    if (auto req = on_decode_response(res)) {
      m_method = req->head->method;
      auto tt = req->tunnel_type;
      if (res->is_tunnel_ok(tt)) m_responded_tunnel_type = tt;
      delete req;
    }
  } else {
    auto head = m_head->as<RequestHead>();
    auto req = new RequestQueue::Request;
    req->head = head;
    req->is_final = head->is_final(m_header_connection);
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
  auto tail = MessageTail::make();
  tail->headSize = m_head_size;
  tail->bodySize = m_body_size;
  m_head_size = 0;
  m_body_size = 0;
  output(MessageEnd::make(tail));
  if (m_is_response) {
    if (m_head->as<ResponseHead>()->is_final(m_header_connection)) {
      on_decode_final();
    }
  }
}

void Decoder::stream_end(StreamEnd *eos) {
  if (m_is_response && (m_state == HEAD || m_state == HEADER) && eos->has_error()) {
    auto status_code = 0;
    auto status_text = ResponseHead::error_to_status(eos->error_code(), status_code);
    auto head = ResponseHead::make();
    head->headers = pjs::Object::make();
    head->protocol = s_http_1_1;
    head->status = status_code;
    head->statusText = status_text;
    output(MessageStart::make(head));
    if (!eos->error().is_undefined()) {
      Data buf;
      Data::Builder db(buf, &s_dp);
      Console::dump(eos->error(), db);
      db.flush();
      output(Data::make(std::move(buf)));
    }
    output(MessageEnd::make());
  }
  output(eos);
}

//
// Encoder
//

Encoder::Encoder(bool is_response, std::shared_ptr<BufferStats> buffer_stats)
  : m_buffer(buffer_stats)
  , m_is_response(is_response)
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
        m_method = nullptr;
        m_is_final = false;
        if (auto req = on_encode_response(head)) {
          m_method = req->head->method;
          m_is_final = req->is_final;
          auto tt = req->tunnel_type;
          if (head->is_tunnel_ok(tt)) m_responded_tunnel_type = tt;
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
      if (m_method == s_HEAD) {
        m_content_length += data->size();
      } else if (m_chunked) {
        if (!data->empty()) output_chunk(*data);
      } else {
        m_buffer.push(*data);
        m_content_length += data->size();
        if (m_buffer.size() > m_buffer_size) {
          m_chunked = true;
          Data body;
          m_buffer.flush(body);
          output_head();
          output_chunk(body);
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
    auto names = m_head->headerNames.get();
    headers->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        if (k == s_keep_alive) return;
        if (k == s_transfer_encoding) return;
        if (k == s_content_length) {
          if (m_method == s_HEAD) {
            no_content_length = true;
          } else {
            return;
          }
        } else if (k == s_connection) {
          if (v.is_string()) {
            m_header_connection = v.s();
            return;
          }
        } else if (k == s_upgrade) {
          if (v.is_string()) m_header_upgrade = v.s();
        }
        pjs::Ref<pjs::Str> name = k;
        if (names) {
          pjs::Value v;
          if (names->get(k, v)) {
            auto s = v.to_string();
            name = s;
            s->release();
          }
        }
        if ((k == s_cookie || k == s_set_cookie) && v.is_array()) {
          v.as<pjs::Array>()->iterate_all(
            [&](pjs::Value &v, int) {
              auto s = v.to_string();
              db.push(name->str());
              db.push(": ");
              db.push(s->str());
              db.push("\r\n");
              s->release();
            }
          );
        } else {
          db.push(name->str());
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
  } else if (m_header_connection) {
    static const std::string str("connection: ");
    db.push(str);
    db.push(m_header_connection->str());
    db.push("\r\n");
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
  if (m_method == s_HEAD) {
    output_head();
  } else if (m_chunked) {
    output(s_dp.make("0\r\n\r\n"));
  } else {
    output_head();
    if (!m_buffer.empty()) {
      output(m_buffer.flush());
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
  : Encoder(false, Filter::buffer_stats())
  , m_options(options)
  , m_handler(handler)
{
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : Filter(r)
  , Encoder(false, Filter::buffer_stats())
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
  : Encoder(true, Filter::buffer_stats())
  , m_options(options)
  , m_handler(handler)
{
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : Filter(r)
  , Encoder(true, Filter::buffer_stats())
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
  Value(options, "maxHeaderSize")
    .get_binary_size(max_header_size)
    .check_nullable();
  Value(options, "maxMessages")
    .get(max_messages)
    .check_nullable();
}

//
// Demux
//

Demux::Demux(const Options &options)
  : DemuxQueue(Filter::buffer_stats())
  , Decoder(false)
  , Encoder(true, Filter::buffer_stats())
  , http2::Server(options)
  , m_options(options)
{
  Decoder::set_max_header_size(options.max_header_size);
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , DemuxQueue(Filter::buffer_stats())
  , Decoder(false)
  , Encoder(true, Filter::buffer_stats())
  , http2::Server(r.m_options)
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
  Decoder::chain(DemuxQueue::input());
  DemuxQueue::chain(Encoder::input());
  Encoder::chain(Filter::output());
  Encoder::set_buffer_size(m_options.buffer_size);
}

void Demux::reset() {
  Filter::reset();
  Decoder::reset();
  Encoder::reset();
  DemuxQueue::reset();
  http2::Server::reset();
  Decoder::chain(DemuxQueue::input());
  m_request_queue.reset();
  m_eos = nullptr;
  m_message_count = 0;
  m_http2 = false;
  m_shutdown = false;
}

void Demux::process(Event *evt) {
  Decoder::input()->input(evt);
  if (auto eos = evt->as<StreamEnd>()) {
    if (DemuxQueue::empty()) {
      Filter::output(evt);
    } else {
      m_eos = eos;
    }
  }
}

void Demux::shutdown() {
  Filter::shutdown();
  if (m_http2) {
    http2::Server::shutdown();
  } else {
    m_shutdown = true;
  }
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

void Demux::on_decode_error() {
  Filter::output(StreamEnd::make());
}

void Demux::on_decode_request(RequestQueue::Request *req) {
  m_request_queue.push(req);
  if (req->tunnel_type != TunnelType::NONE) {
    DemuxQueue::wait_output();
    if (req->tunnel_type == TunnelType::HTTP2) {
      req->head->headers->ht_delete(s_upgrade);
      req->head->headers->ht_delete(s_http2_settings);
      m_http2 = true;
      http2::Server::chain(Filter::output());
      Decoder::chain(http2::Server::initial_stream());
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
}

auto Demux::on_encode_response(ResponseHead *head) -> RequestQueue::Request* {
  if (head->status == 100) {
    DemuxQueue::increase_output_count(1);
    return nullptr;
  } else {
    auto req = m_request_queue.shift();
    m_message_count++;
    if (
      (m_options.max_messages > 0 && m_message_count >= m_options.max_messages) ||
      (m_shutdown && m_request_queue.empty())
    ) {
      req->is_final = true;
    }
    return req;
  }
}

bool Demux::on_decode_tunnel(TunnelType tt) {
  if (tt == TunnelType::HTTP2) {
    m_http2 = true;
    http2::Server::chain(Filter::output());
    http2::Server::init();
    Decoder::chain(http2::Server::input());
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
    DemuxQueue::dedicate();
  }
  return true;
}

//
// Mux::Options
//

Mux::Options::Options(pjs::Object *options)
  : MuxSession::Options(options)
  , http2::Endpoint::Options(options)
{
  Value(options, "bufferSize")
    .get_binary_size(buffer_size)
    .check_nullable();
  Value(options, "maxHeaderSize")
    .get_binary_size(max_header_size)
    .check_nullable();
  Value(options, "version")
    .get(version)
    .get(version_s)
    .get(version_f)
    .check_nullable();
  Value(options, "ping")
    .get(ping_f)
    .check_nullable();
}

//
// Mux
//

Mux::Mux()
  : m_waiting_events(Filter::buffer_stats())
{
}

Mux::Mux(pjs::Function *session_selector)
  : MuxBase(session_selector)
  , m_waiting_events(Filter::buffer_stats())
{
}

Mux::Mux(pjs::Function *session_selector, const Options &options)
  : MuxBase(session_selector)
  , m_options(options)
  , m_waiting_events(Filter::buffer_stats())
{
}

Mux::Mux(pjs::Function *session_selector, pjs::Function *options)
  : MuxBase(session_selector, options)
  , m_waiting_events(Filter::buffer_stats())
{
}

Mux::Mux(const Mux &r)
  : MuxBase(r)
  , m_options(r.m_options)
  , m_waiting_events(r.m_waiting_events)
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

auto Mux::on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* {
  if (options) {
    try {
      Options opts(options);
      return new SessionPool(opts, Filter::buffer_stats());
    } catch (std::runtime_error &err) {
      Filter::error(err.what());
      return nullptr;
    }
  } else {
    return new SessionPool(m_options, Filter::buffer_stats());
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

Mux::Session::Session(const Mux::Options &options, std::shared_ptr<BufferStats> buffer_stats)
  : MuxQueue(this)
  , Encoder(false, buffer_stats)
  , Decoder(true)
  , http2::Client(options)
  , m_options(options)
{
  Decoder::set_max_header_size(options.max_header_size);
}

Mux::Session::~Session() {
  if (m_version_selector) {
    m_version_selector->close();
  }
  if (m_ping_promise_cb) {
    m_ping_promise_cb->discard();
  }
}

void Mux::Session::mux_session_open(MuxSource *source) {
  auto mux = static_cast<Mux*>(source);
  if (!select_protocol(mux)) {
    MuxSession::set_pending(true);
    MuxSession::input()->flush();
  }
}

auto Mux::Session::mux_session_open_stream(MuxSource *source) -> EventFunction* {
  if (m_http2) {
    return http2::Client::stream([=]() { source->discard(); });
  } else {
    return MuxQueue::stream(source);
  }
}

void Mux::Session::mux_session_close_stream(EventFunction *stream) {
  if (m_http2) {
    http2::Client::close(stream);
  } else {
    MuxQueue::close(stream);
  }
}

void Mux::Session::mux_session_close() {
  if (m_http2) {
    if (m_ping_promise_cb) {
      m_ping_promise_cb->discard();
      m_ping_promise_cb = nullptr;
      m_ping_context = nullptr;
    }
    http2::Client::shutdown();
  } else {
    MuxQueue::reset();
    m_request_queue.reset();
  }
}

void Mux::Session::on_encode_request(RequestQueue::Request *req) {
  m_request_queue.push(req);
}

auto Mux::Session::on_decode_response(ResponseHead *head) -> RequestQueue::Request* {
  if (head->status == 100) {
    MuxQueue::increase_output_count(1);
    return nullptr;
  } else {
    return m_request_queue.shift();
  }
}

bool Mux::Session::on_decode_tunnel(TunnelType tt) {
  Encoder::set_tunnel();
  MuxQueue::dedicate();
  MuxSession::detach(); // so that it won't get allocated for other sources
  return true;
}

void Mux::Session::on_decode_final() {
  MuxSession::end(StreamEnd::make());
}

void Mux::Session::on_decode_error()
{
}

void Mux::Session::on_ping(const Data &data) {
  if (m_options.ping_f) {
    pjs::Ref<Data> d(Data::make(data));
    schedule_ping(d);
  }
}

void Mux::Session::on_queue_end(StreamEnd *eos) {
  MuxSession::end(eos);
}

void Mux::Session::on_endpoint_close(StreamEnd *eos) {
  MuxSession::end(eos);
}

bool Mux::Session::select_protocol(Mux *mux) {
  thread_local static auto s_method_select = pjs::ClassDef<VersionSelector>::method("select");

  if (m_version_selected) return true;

  if (m_options.version_f) {
    pjs::Value ret;
    if (!mux->eval(m_options.version_f, ret)) return false;
    if (ret.is<pjs::Promise>()) {
      m_version_selector = VersionSelector::make(mux, this);
      ret.as<pjs::Promise>()->then(nullptr, pjs::Function::make(s_method_select, m_version_selector));
      return false;
    }
    return select_protocol(mux, ret);
  } else if (m_options.version_s) {
    return select_protocol(mux, m_options.version_s.get());
  } else {
    return select_protocol(mux, m_options.version);
  }
}

bool Mux::Session::select_protocol(Mux *mux, const pjs::Value &version) {
  if (version.is_number()) {
    m_version_selected = mux->verify_http_version(version.to_int32());
  } else if (version.is_string()) {
    m_version_selected = mux->verify_http_version(version.s());
  } else {
    mux->error("invalid HTTP version");
  }

  switch (m_version_selected) {
  case 1:
    MuxQueue::chain(Encoder::input());
    Encoder::chain(MuxSession::input());
    MuxSession::chain(Decoder::input());
    Decoder::chain(MuxQueue::reply());
    Encoder::set_buffer_size(m_options.buffer_size);
    MuxSession::set_pending(false);
    return true;
  case 2:
    m_http2 = true;
    http2::Client::chain(MuxSession::input());
    MuxSession::chain(http2::Client::reply());
    MuxSession::set_pending(false);
    if (m_options.ping_f) {
      m_ping_context = mux->context();
      schedule_ping();
    }
    return true;
  default:
    break;
  }

  return false;
}

void Mux::Session::schedule_ping(Data *ack) {
  pjs::Value arg(ack), ret;
  (*m_options.ping_f)(*m_ping_context, 1, &arg, ret);
  if (!m_ping_context->ok()) return;
  if (ret.is_nullish()) return;
  if (ret.is<Data>()) return http2::Client::ping(*ret.as<Data>());
  if (ret.is_promise()) {
    m_ping_promise_cb = pjs::Promise::Callback::make(
      [this](pjs::Promise::State, const pjs::Value &value) {
        if (value.is<Data>()) {
          http2::Client::ping(*value.as<Data>());
        }
      }
    );
    ret.as<pjs::Promise>()->then(m_ping_context, m_ping_promise_cb->resolved());
  }
}

//
// Server
//

Server::Server(pjs::Object *handler, const Options &options)
  : Demux(options)
  , m_handler(handler)
{
}

Server::Server(const Server &r)
  : Demux(r)
  , m_handler(r.m_handler)
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

auto Server::on_demux_open_stream() -> EventFunction* {
  auto handler = Handler::make(this);
  handler->retain();
  return handler;
}

void Server::on_demux_close_stream(EventFunction *stream) {
  static_cast<Handler*>(stream)->release();
}

void Server::on_demux_queue_dedicate(EventFunction *stream) {
  if (num_sub_pipelines() > 0) {
    auto p = sub_pipeline(0, false, Filter::output())->start();
    static_cast<Handler*>(stream)->tunnel(p);
  }
}

//
// Server::Handler
//

void Server::Handler::on_event(Event *evt) {
  if (m_tunnel) {
    m_tunnel->input()->input(evt);
    return;
  }

  if (auto req = m_message_reader.read(evt)) {
    pjs::Ref<Message> res;

    if (auto &handler = m_server->m_handler) {
      if (handler->is_instance_of<Message>()) {
        res = handler->as<Message>();

      } else if (handler->is_function()) {
        pjs::Value arg(req), ret;
        if (!m_server->callback(handler->as<pjs::Function>(), 1, &arg, ret)) return;
        if (ret.is_object()) {
          if (auto obj = ret.o()) {
            if (obj->is_instance_of<Message>()) {
              res = obj->as<Message>();
            } else if (obj->is<pjs::Promise>()) {
              obj->as<pjs::Promise>()->then(
                nullptr,
                pjs::Promise::Callback::resolved(),
                pjs::Promise::Callback::rejected()
              );
              req->release();
              return;
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

void Server::Handler::on_resolved(const pjs::Value &value) {
  if (value.is<Message>()) {
    value.as<Message>()->write(EventFunction::output());
  } else {
    m_server->Filter::error("Promise did not resolve to a Message");
  }
}

void Server::Handler::on_rejected(const pjs::Value &error) {
  m_server->Filter::error(StreamEnd::make(error));
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
  m_request_head = nullptr;
  m_promise_callback = nullptr;
  m_buffer.clear();
  m_message_reader.reset();
}

void TunnelServer::process(Event *evt) {
  if (m_pipeline) {
    m_pipeline->input()->input(evt);

  } else if (m_promise_callback) {
    m_buffer.push(evt);

  } else if (auto req = m_message_reader.read(evt)) {
    m_request_head = pjs::coerce<RequestHead>(req->head());

    pjs::Value arg(req), ret;
    req->release();
    if (!callback(m_handler, 1, &arg, ret)) return;

    pjs::Ref<Message> res;
    if (ret.is_nullish()) {
      res = Message::make();
    } else if (ret.is_instance_of<Message>()) {
      res = ret.as<Message>();
    } else if (ret.is_promise()) {
      m_promise_callback = pjs::Promise::Callback::make(
        [this](pjs::Promise::State state, const pjs::Value &v) {
          on_resolve(state, v);
        }
      );
      ret.as<pjs::Promise>()->then(Filter::context(), m_promise_callback->resolved());
      return;
    } else {
      Filter::error("handler did not return a Message");
      return;
    }

    start_tunnel(res);
  }
}

void TunnelServer::on_resolve(pjs::Promise::State state, const pjs::Value &value) {
  pjs::Ref<Message> res;
  if (value.is_nullish()) {
    start_tunnel(Message::make());
  } else if (value.is_instance_of<Message>()) {
    start_tunnel(value.as<Message>());
  } else {
    Filter::error("Promise did not resolve to a Message");
  }
}

void TunnelServer::start_tunnel(Message *response) {
  pjs::Ref<ResponseHead> response_head = pjs::coerce<ResponseHead>(response->head());
  if (response_head->is_tunnel_ok(m_request_head->tunnel_type())) {
    m_pipeline = sub_pipeline(0, true, Filter::output());
  }

  response->write(Filter::output());

  if (m_pipeline) {
    m_pipeline->start();
    m_buffer.flush(m_pipeline->input());
  }
}

//
// TunnelClient
//

TunnelClient::Options::Options(pjs::Object *options) {
  Value(options, "onState")
    .get(on_state_f)
    .check_nullable();
}

TunnelClient::TunnelClient(pjs::Object *handshake)
  : m_handshake(handshake)
{
}

TunnelClient::TunnelClient(pjs::Object *handshake, const Options &options)
  : m_handshake(handshake)
  , m_options(options)
{
}

TunnelClient::TunnelClient(const TunnelClient &r)
  : Filter(r)
  , m_handshake(r.m_handshake)
  , m_options(r.m_options)
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
  EventSource::close();
  m_buffer.clear();
  m_pipeline = nullptr;
  m_request_head = nullptr;
  m_response_head = nullptr;
  m_eos = nullptr;
  m_on_state_change = nullptr;
  m_is_tunnel_started = false;
}

void TunnelClient::process(Event *evt) {
  if (!m_pipeline) {
    if (m_options.on_state_f) {
      m_on_state_change = [=](State state) {
        pjs::Value arg(pjs::EnumDef<State>::name(state)), ret;
        Filter::callback(m_options.on_state_f, 1, &arg, ret);
      };
    }
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
    Filter::output(msg->as<Message>(), m_pipeline->input());
    if (m_on_state_change) {
      m_on_state_change(State::connecting);
    }
  }

  if (m_is_tunnel_started) {
    if (!m_buffer.empty()) {
      Filter::output(Data::make(std::move(m_buffer)), m_pipeline->input());
    }
    Filter::output(evt, m_pipeline->input());
  } else if (auto *data = evt->as<Data>()) {
    m_buffer.push(*data);
  } else if (auto eos = evt->as<StreamEnd>()) {
    m_eos = eos;
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
      if (m_request_head) {
        auto tt = m_request_head->tunnel_type();
        if (m_response_head->is_tunnel_ok(tt)) {
          m_is_tunnel_started = true;
          if (m_on_state_change) {
            m_on_state_change(State::connected);
          }
          if (m_eos) {
            EventFunction::input()->input_async(m_eos);
          } else {
            EventFunction::input()->flush_async();
          }
        } else {
          if (m_on_state_change) {
            m_on_state_change(State::closed);
          }
          Filter::output(StreamEnd::make());
        }
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

template<> void ClassDef<Server::Handler>::init() {
  super<Promise::Callback>();
}

template<> void EnumDef<TunnelClient::State>::init() {
  define(TunnelClient::State::idle, "idle");
  define(TunnelClient::State::connecting, "connecting");
  define(TunnelClient::State::connected, "connected");
  define(TunnelClient::State::closed, "closed");
}

} // namespace pjs
