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
#include <limits>

namespace pipy {
namespace http {

static const pjs::Ref<pjs::Str> s_protocol(pjs::Str::make("protocol"));
static const pjs::Ref<pjs::Str> s_method(pjs::Str::make("method"));
static const pjs::Ref<pjs::Str> s_HEAD(pjs::Str::make("HEAD"));
static const pjs::Ref<pjs::Str> s_CONNECT(pjs::Str::make("CONNECT"));
static const pjs::Ref<pjs::Str> s_path(pjs::Str::make("path"));
static const pjs::Ref<pjs::Str> s_status(pjs::Str::make("status"));
static const pjs::Ref<pjs::Str> s_status_text(pjs::Str::make("statusText"));
static const pjs::Ref<pjs::Str> s_headers(pjs::Str::make("headers"));
static const pjs::Ref<pjs::Str> s_http_1_0(pjs::Str::make("HTTP/1.0"));
static const pjs::Ref<pjs::Str> s_http_1_1(pjs::Str::make("HTTP/1.1"));
static const pjs::Ref<pjs::Str> s_connection(pjs::Str::make("connection"));
static const pjs::Ref<pjs::Str> s_keep_alive(pjs::Str::make("keep-alive"));
static const pjs::Ref<pjs::Str> s_set_cookie(pjs::Str::make("set-cookie"));
static const pjs::Ref<pjs::Str> s_close(pjs::Str::make("close"));
static const pjs::Ref<pjs::Str> s_transfer_encoding(pjs::Str::make("transfer-encoding"));
static const pjs::Ref<pjs::Str> s_content_length(pjs::Str::make("content-length"));
static const pjs::Ref<pjs::Str> s_content_encoding(pjs::Str::make("content-encoding"));
static const pjs::Ref<pjs::Str> s_upgrade(pjs::Str::make("upgrade"));
static const pjs::Ref<pjs::Str> s_websocket(pjs::Str::make("websocket"));
static const pjs::Ref<pjs::Str> s_h2c(pjs::Str::make("h2c"));
static const pjs::Ref<pjs::Str> s_bad_gateway(pjs::Str::make("Bad Gateway"));
static const pjs::Ref<pjs::Str> s_cannot_resolve(pjs::Str::make("Cannot Resolve"));
static const pjs::Ref<pjs::Str> s_connection_refused(pjs::Str::make("Connection Refused"));
static const pjs::Ref<pjs::Str> s_unauthorized(pjs::Str::make("Unauthorized"));
static const pjs::Ref<pjs::Str> s_read_error(pjs::Str::make("Read Error"));
static const pjs::Ref<pjs::Str> s_write_error(pjs::Str::make("Write Error"));
static const pjs::Ref<pjs::Str> s_gateway_timeout(pjs::Str::make("Gateway Timeout"));
static const pjs::Ref<pjs::Str> s_http2_preface_method(pjs::Str::make("PRI"));
static const pjs::Ref<pjs::Str> s_http2_preface_path(pjs::Str::make("*"));
static const pjs::Ref<pjs::Str> s_http2_preface_protocol(pjs::Str::make("HTTP/2.0"));

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
  m_is_final = false;
  m_is_bodiless = false;
  m_is_connect = false;
  m_is_upgrade_websocket = false;
  m_is_upgrade_http2 = false;
  m_is_tunnel = false;
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

  while (!data->empty()) {
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
          } else {
            req->method(method);
            req->path(path);
            req->protocol(protocol);
            m_head = req;
            m_is_bodiless = (method == s_HEAD);
            m_is_connect = (method == s_CONNECT);
          }
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
                headers->set(key, val.get());
                if (key == s_upgrade) {
                  if (val == s_websocket) {
                    m_is_upgrade_websocket = true;
                  } else if (val == s_h2c) {
                    m_is_upgrade_http2 = true;
                  }
                }
              }
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
            } else if (m_is_response && !m_is_bodiless && !m_is_connect && !m_is_upgrade_websocket) {
              auto status = m_head->as<ResponseHead>()->status();
              if (status >= 200 && status != 204 && status != 304) {
                m_body_size = std::numeric_limits<int>::max();
              }
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
  output(MessageStart::make(m_head));
}

void Decoder::message_end() {
  if (m_is_connect || m_is_upgrade_websocket) {
    m_is_tunnel = true;
  }

  output(MessageEnd::make());

  m_is_final = false;
  m_is_bodiless = false;
  m_is_connect = false;
  m_is_upgrade_websocket = false;
  m_is_upgrade_http2 = false;
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

void Encoder::reset() {
  m_buffer.clear();
  m_start = nullptr;
  m_is_final = false;
  m_is_bodiless = false;
  m_is_connect = false;
  m_is_upgrade_websocket = false;
  m_is_upgrade_http2 = false;
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

    if (auto head = start->head()) {
      if (!m_is_response) {
        pjs::Value method;
        head->get(s_method, method);
        if (method.is_string()) {
          if (method.s() == s_HEAD) {
            m_is_bodiless = true;
          } else if (method.s() == s_CONNECT) {
            m_is_connect = true;
          }
        }
      }
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

    if (m_is_connect || m_is_upgrade_websocket) {
      m_is_tunnel = true;
    }

    m_buffer.clear();
    m_start = nullptr;
    m_is_final = false;
    m_is_bodiless = false;
    m_is_connect = false;
    m_is_upgrade_websocket = false;
    m_is_upgrade_http2 = false;

  } else if (evt->is<StreamEnd>()) {
    output(evt);
    m_buffer.clear();
    m_start = nullptr;
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
        if (k == s_connection) return;
        if (k == s_keep_alive) return;
        if (k == s_transfer_encoding) return;
        if (k == s_content_length) {
          if (is_bodiless_response()) {
            content_length_written = true;
          } else {
            return;
          }
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
          if (k == s_upgrade) {
            static std::string str("connection: upgrade\r\n");
            if (s == s_websocket) {
              m_is_upgrade_websocket = true;
              s_dp.push(buffer, str);
            } else if (s == s_h2c) {
              m_is_upgrade_http2 = true;
              s_dp.push(buffer, str);
            }
          }
          s->release();
        }
      }
    );
  }

  if (m_chunked) {
    static std::string str("transfer-encoding: chunked\r\n");
    s_dp.push(buffer, str);
  } else if (!content_length_written) {
    char str[100];
    std::sprintf(str, ": %d\r\n", m_content_length);
    s_dp.push(buffer, s_content_length->str());
    s_dp.push(buffer, str);
  }

  if (!m_is_upgrade_websocket && !m_is_upgrade_http2) {
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

RequestEncoder::RequestEncoder(pjs::Object *options)
  : m_ef_encode(false)
{
  if (options) {
    pjs::Value buffer_size;
    options->get("bufferSize", buffer_size);

    if (!buffer_size.is_undefined()) {
      if (buffer_size.is_number()) {
        m_buffer_size = buffer_size.n();
      } else if (buffer_size.is_string()) {
        m_buffer_size = utils::get_byte_size(buffer_size.s()->str());
      } else {
        throw std::runtime_error("options.bufferSize requires a number or a string");
      }
    }
  }
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : Filter(r)
  , m_ef_encode(false)
  , m_buffer_size(r.m_buffer_size)
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
  m_ef_encode.set_buffer_size(m_buffer_size);
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
    pjs::Value buffer_size;
    options->get("final", m_final);
    options->get("bodiless", m_bodiless);
    options->get("bufferSize", buffer_size);

    if (!buffer_size.is_undefined()) {
      if (buffer_size.is_number()) {
        m_buffer_size = buffer_size.n();
      } else if (buffer_size.is_string()) {
        m_buffer_size = utils::get_byte_size(buffer_size.s()->str());
      } else {
        throw std::runtime_error("options.bufferSize requires a number or a string");
      }
    }
  }
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : Filter(r)
  , m_ef_encode(true)
  , m_final(r.m_final)
  , m_bodiless(r.m_bodiless)
  , m_buffer_size(r.m_buffer_size)
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
  m_ef_encode.set_buffer_size(m_buffer_size);
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
      pjs::Value final, bodiless;
      if (!eval(m_final, final)) return;
      if (!eval(m_bodiless, bodiless)) return;
      m_ef_encode.set_final(final.to_boolean());
      m_ef_encode.set_bodiless(bodiless.to_boolean());
    }
    output(evt, m_ef_encode.input());
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
  m_started = false;
}

void RequestQueue::shutdown() {
  if (!m_queue.empty()) {
    m_queue.tail()->is_final = true;
  }
}

void RequestQueue::on_input(Event *evt) {
  if (evt->is<MessageStart>()) {
    auto *r = new RequestQueue::Request;
    on_enqueue(r);
    m_queue.push(r);
  }

  forward(evt);
}

void RequestQueue::on_reply(Event *evt) {
  if (evt->is<MessageStart>()) {
    if (auto *r = m_queue.head()) {
      on_dequeue(r);
      m_queue.remove(r);
      delete r;
    }
  } else if (evt->is<StreamEnd>()) {
    reset();
  }

  output(evt);
}

//
// Demux
//

Demux::Demux(pjs::Object *options)
  : QueueDemuxer(true)
  , Decoder(false)
  , Encoder(true)
{
  if (options) {
    pjs::Value buffer_size;

    options->get("bufferSize", buffer_size);

    if (!buffer_size.is_undefined()) {
      if (buffer_size.is_number()) {
        m_buffer_size = buffer_size.n();
      } else if (buffer_size.is_string()) {
        m_buffer_size = utils::get_byte_size(buffer_size.s()->str());
      } else {
        throw std::runtime_error("options.bufferSize requires a number or a string");
      }
    }
  }
}

Demux::Demux(const Demux &r)
  : Filter(r)
  , QueueDemuxer(true)
  , Decoder(false)
  , Encoder(true)
  , m_buffer_size(r.m_buffer_size)
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
  Decoder::chain(RequestQueue::input());
  RequestQueue::chain_forward(QueueDemuxer::input());
  QueueDemuxer::chain(RequestQueue::reply());
  RequestQueue::chain(Encoder::input());
  Encoder::chain(Filter::output());
  Encoder::set_buffer_size(m_buffer_size);
}

void Demux::reset() {
  Filter::reset();
  RequestQueue::reset();
  QueueDemuxer::reset();
  Decoder::reset();
  Encoder::reset();
  if (m_http2_demuxer) {
    Decoder::chain(RequestQueue::input());
    RequestQueue::chain_forward(QueueDemuxer::input());
    delete m_http2_demuxer;
    m_http2_demuxer = nullptr;
  }
}

void Demux::process(Event *evt) {
  Filter::output(evt, Decoder::input());
}

void Demux::shutdown() {
  Filter::shutdown();
  if (RequestQueue::empty()) {
    Filter::output(StreamEnd::make());
  } else {
    RequestQueue::shutdown();
  }
  if (m_http2_demuxer) {
    m_http2_demuxer->shutdown();
  }
}

auto Demux::on_new_sub_pipeline() -> Pipeline* {
  return sub_pipeline(0, true);
}

void Demux::on_enqueue(Request *req) {
  req->is_final = Decoder::is_final();
  req->is_bodiless = Decoder::is_bodiless();
  req->is_connect = Decoder::is_connect();
  if (Decoder::is_connect() || Decoder::is_upgrade_websocket()) {
    isolate();
  } else if (Decoder::is_upgrade_http2()) {
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
    RequestQueue::chain_forward(m_http2_demuxer->initial_stream());
  }
}

void Demux::on_dequeue(Request *req) {
  Encoder::set_final(req->is_final);
  Encoder::set_bodiless(req->is_bodiless);
  Encoder::set_connect(req->is_connect);
}

void Demux::on_http2_pass() {
  upgrade_http2();
  Decoder::chain(m_http2_demuxer->input());
}

void Demux::upgrade_http2() {
  if (!m_http2_demuxer) {
    m_http2_demuxer = new HTTP2Demuxer(this);
    m_http2_demuxer->chain(Filter::output());
  }
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(const pjs::Value &key, pjs::Object *options)
  : pipy::Mux(key, options)
{
  if (options) {
    pjs::Value version, buffer_size;

    options->get("version", version);
    options->get("bufferSize", buffer_size);

    if (!version.is_undefined()) {
      if (version.is_number()) {
        m_version = version.n();
        if (m_version != 1 && m_version != 2) {
          std::string msg("invalid HTTP version: ");
          throw std::runtime_error(msg + std::to_string(m_version));
        }
      } else {
        throw std::runtime_error("options.version requires a number");
      }
    }

    if (!buffer_size.is_undefined()) {
      if (buffer_size.is_number()) {
        m_buffer_size = buffer_size.n();
      } else if (buffer_size.is_string()) {
        m_buffer_size = utils::get_byte_size(buffer_size.s()->str());
      } else {
        throw std::runtime_error("options.bufferSize requires a number or a string");
      }
    }
  }
}

Mux::Mux(const Mux &r)
  : pipy::Mux(r)
  , m_version(r.m_version)
  , m_buffer_size(r.m_buffer_size)
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

auto Mux::on_new_session() -> MuxBase::Session* {
  return new Session(m_version, m_buffer_size);
}

//
// Mux::Session
//

void Mux::Session::open() {
  switch (m_version) {
  case 1:
    QueueMuxer::chain(Encoder::input());
    Encoder::chain(RequestQueue::input());
    RequestQueue::chain_forward(MuxBase::Session::input());
    MuxBase::Session::chain(Decoder::input());
    Decoder::chain(RequestQueue::reply());
    RequestQueue::chain(QueueMuxer::reply());
    break;
  case 2:
    upgrade_http2();
    break;
  }
  Encoder::set_buffer_size(m_buffer_size);
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
    return m_http2_muxer->close(stream);
  } else {
    QueueMuxer::close(stream);
  }
}

void Mux::Session::close() {
  QueueMuxer::reset();
  RequestQueue::reset();
  if (m_http2_muxer) {
    m_http2_muxer->close();
    delete m_http2_muxer;
    m_http2_muxer = nullptr;
  }
}

void Mux::Session::on_enqueue(Request *req) {
  req->is_bodiless = Encoder::is_bodiless();
  req->is_connect = Encoder::is_connect();
  if (Encoder::is_connect() || Encoder::is_upgrade_websocket()) {
    MuxBase::Session::isolate();
    QueueMuxer::isolate();
  }
}

void Mux::Session::on_dequeue(Request *req) {
  Decoder::set_bodiless(req->is_bodiless);
  Decoder::set_connect(req->is_connect);
}

void Mux::Session::upgrade_http2() {
  if (!m_http2_muxer) {
    m_http2_muxer = new HTTP2Muxer();
    m_http2_muxer->open(static_cast<MuxBase::Session*>(this));
  }
}

//
// Server
//

Server::Server(const std::function<Message*(Message*)> &handler)
  : m_handler_func(handler)
  , m_ef_decoder(false)
  , m_ef_encoder(true)
  , m_ef_handler(this)
{
}

Server::Server(pjs::Object *handler)
  : m_handler_obj(handler)
  , m_ef_decoder(false)
  , m_ef_encoder(true)
  , m_ef_handler(this)
{
}

Server::Server(const Server &r)
  : Filter(r)
  , m_handler_func(r.m_handler_func)
  , m_handler_obj(r.m_handler_obj)
  , m_ef_decoder(false)
  , m_ef_encoder(true)
  , m_ef_handler(this)
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
  m_ef_handler.m_shutdown = false;
  m_tunnel = nullptr;
}

void Server::process(Event *evt) {
  output(evt, m_ef_decoder.input());
}

void Server::shutdown() {
  Filter::shutdown();
  m_ef_handler.m_shutdown = true;
}

void Server::on_tunnel_data(Data *data) {
  if (!m_tunnel) {
    if (num_sub_pipelines() > 0) {
      m_tunnel = sub_pipeline(0, false);
    }
  }

  if (m_tunnel) {
    m_tunnel->input()->input(data);
  }
}

void Server::Handler::on_event(Event *evt) {
  if (m_server->m_ef_decoder.is_tunnel()) {
    if (auto data = evt->as<Data>()) {
      m_server->on_tunnel_data(data);
    }

  } else if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
      m_buffer.clear();
      auto &encoder = m_server->m_ef_encoder;
      auto &decoder = m_server->m_ef_decoder;
      encoder.set_bodiless(decoder.is_bodiless());
      encoder.set_final(m_shutdown || decoder.is_final());
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
        res = func(req);

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
  }
}

//
// Client
//

void ClientReceiver::on_event(Event *evt) {
  static_cast<Client*>(this)->on_receive(evt);
}

Client::Client()
  : m_ef_encoder(false)
  , m_ef_decoder(true)
{
}

Client::Client(const Client &r)
  : Filter(r)
  , m_ef_encoder(false)
  , m_ef_decoder(true)
{
}

Client::~Client() {
}

void Client::dump(std::ostream &out) {
  out << "sendHTTP";
}

auto Client::clone() -> Filter* {
  return new Client(*this);
}

void Client::reset() {
  Filter::reset();
  m_ef_encoder.reset();
  m_ef_decoder.reset();
  m_pipeline = nullptr;
  m_request_end = false;
}

void Client::process(Event *evt) {
  if (evt->is<MessageStart>()) {
    if (!m_pipeline) {
      m_pipeline = sub_pipeline(0, false);
      m_pipeline->chain(m_ef_decoder.input());
      m_ef_decoder.chain(ClientReceiver::input());
      m_ef_encoder.chain(m_pipeline->input());
      m_ef_encoder.set_final(true);
      output(evt, m_ef_encoder.input());
      m_ef_decoder.set_bodiless(m_ef_encoder.is_bodiless());
      m_ef_decoder.set_connect(m_ef_encoder.is_connect());
    }
  } else if (evt->is<Data>()) {
    if (m_pipeline && !m_request_end) {
      output(evt, m_ef_encoder.input());
    }
  } else if (evt->is<MessageEnd>()) {
    if (!m_request_end) {
      output(evt, m_ef_encoder.input());
      m_request_end = true;
    }
  } else if (evt->is<StreamEnd>()) {
    if (m_pipeline) {
      output(evt, m_pipeline->input());
    }
  }
}

void Client::on_receive(Event *evt) {
  if (evt->is<MessageEnd>()) {
    if (m_pipeline) {
      m_pipeline->input()->input(StreamEnd::make());
      m_pipeline = nullptr;
    }
  }
  output(evt);
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

void TunnelServer::dump(std::ostream &out) {
  out << "acceptHTTPTunnel";
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
          head->get(s_status, status);
          if (status.is_undefined() || (status.is_number() && 100 <= status.n() && status.n() < 300)) {
            m_pipeline = sub_pipeline(0, true);
            m_pipeline->chain(output());
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

void TunnelClient::dump(std::ostream &out) {
  out << "connectHTTPTunnel";
}

auto TunnelClient::clone() -> Filter* {
  return new TunnelClient(*this);
}

void TunnelClient::reset() {
  Filter::reset();
  TunnelClientReceiver::close();
  m_pipeline = nullptr;
  m_is_tunneling = false;
}

void TunnelClient::process(Event *evt) {
  if (!m_pipeline) {
    pjs::Value handshake;
    if (!eval(m_handshake, handshake)) return;
    if (!handshake.is<Message>()) {
      Log::error("[connectHTTPTunnel] invalid handshake request");
      return;
    }
    m_pipeline = sub_pipeline(0, true);
    m_pipeline->chain(TunnelClientReceiver::input());
    auto inp = m_pipeline->input();
    auto msg = handshake.as<Message>();
    inp->input(MessageStart::make(msg->head()));
    if (auto body = msg->body()) inp->input(body);
    inp->input(MessageEnd::make());
  }

  if (m_pipeline) {
    m_pipeline->input()->input(evt);
  }
}

void TunnelClient::on_receive(Event *evt) {
  if (m_is_tunneling) {
    output(evt);
  } else if (auto start = evt->as<MessageStart>()) {
    m_is_tunneling = true;
  }
}

} // namespace http
} // namespace pipy
