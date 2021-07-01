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
#include "context.hpp"
#include "inbound.hpp"
#include "logging.hpp"

namespace pipy {
namespace http {

static void parse_transfer_encoding(
  const std::string &transfer_encoding,
  int &content_length,
  bool &is_chunked
) {
  is_chunked = false;
  for (size_t i = 0; i < transfer_encoding.size(); i++) {
    size_t j = i; while (std::isalpha(transfer_encoding[j])) j++;
    if (j - i == 7 && !std::strncmp(&transfer_encoding[i], "chunked", 7)) is_chunked = true;
    i = j + 1;
  }
  if (is_chunked) content_length = 0;
}

static bool is_content_length(const std::string &s) {
  static const std::string lowercase("content-length");
  if (s.length() != lowercase.length()) return false;
  for (size_t i = 0; i < lowercase.length(); i++) {
    if (std::tolower(s[i]) != lowercase[i]) {
      return false;
    }
  }
  return true;
}

//
// RequestDecoder
//

RequestDecoder::RequestDecoder()
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
    "decodeHttpRequest()",
    "Deframes an HTTP request message",
  };
}

void RequestDecoder::dump(std::ostream &out) {
  out << "decodeHttpRequest";
}

auto RequestDecoder::clone() -> Filter* {
  return new RequestDecoder(*this);
}

void RequestDecoder::reset() {
  m_state = METHOD;
  m_name.clear();
  m_head = nullptr;
  m_session_end = false;
}

void RequestDecoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  // Data
  if (auto data = inp->as<Data>()) {
    while (!data->empty()) {
      auto is_body = (m_state == BODY);
      auto is_end = false;

      // Read up to body data boundaries
      pjs::Ref<Data> read(Data::make());
      data->shift_to([&](int c) {

        // Parse one character
        switch (m_state) {

        // Read method
        case METHOD:
          if (c == ' ') {
            m_head = RequestHead::make();
            m_head->headers(pjs::Object::make());
            m_head->method(pjs::Str::make(m_name.str()));
            m_state = PATH;
            m_name.clear();
          } else {
            m_name.push(c);
          }
          break;

        // Read path
        case PATH:
          if (c == ' ') {
            m_head->path(pjs::Str::make(m_name.str()));
            m_state = PROTOCOL;
            m_name.clear();
          } else {
            m_name.push(c);
          }
          break;

        // Read protocol
        case PROTOCOL:
          if (c == '\n') {
            m_protocol = m_name.str();
            m_head->protocol(pjs::Str::make(m_protocol));
            m_state = HEADER_NAME;
            m_name.clear();
            m_transfer_encoding.clear();
            m_connection.clear();
            m_keep_alive.clear();
            m_chunked = false;
            m_content_length = 0;
          } else {
            m_name.push(c);
          }
          break;

        // Read header name
        case HEADER_NAME:
          if (c == ':') {
            m_state = HEADER_VALUE;
            m_value.clear();
          } else if (c == '\n' && m_name.empty()) {
            output(MessageStart::make(m_head));
            parse_transfer_encoding(m_transfer_encoding, m_content_length, m_chunked);
            if (m_chunked) {
              m_state = CHUNK_HEAD;
            } else if (m_content_length > 0) {
              m_state = BODY;
              return true;
            } else {
              end_message(ctx);
              m_state = METHOD;
              m_name.clear();
            }
          } else {
            m_name.push(c);
          }
          break;

        // Read header value
        case HEADER_VALUE:
          if (c == '\n') {
            auto name = m_name.str();
            for (auto &ch : name) ch = std::tolower(ch);
            if (name == "content-length") m_content_length = std::atoi(m_value.c_str());
            else if (name == "transfer-encoding") m_transfer_encoding = m_value.str();
            else if (name == "connection") m_connection = m_value.str();
            else if (name == "keep-alive") m_keep_alive = m_value.str();
            else if (auto headers = m_head->as<RequestHead>()->headers()) {
              pjs::Ref<pjs::Str> key(pjs::Str::make(name));
              headers->ht_set(key, m_value.str());
            }
            m_state = HEADER_NAME;
            m_name.clear();
          } else {
            m_value.push(c);
          }
          break;

        // Read body
        case BODY:
          if (!--m_content_length) {
            if (m_chunked) {
              m_state = CHUNK_TAIL;
            } else {
              m_state = METHOD;
              m_name.clear();
              is_end = true;
            }
            return true;
          }
          break;

        // Read chunk length
        case CHUNK_HEAD:
          if (c == '\n') {
            if (m_content_length > 0) {
              m_state = BODY;
              return true;
            } else {
              m_state = CHUNK_TAIL_LAST;
            }
          }
          else if ('0' <= c && c <= '9') m_content_length = (m_content_length << 4) + (c - '0');
          else if ('a' <= c && c <= 'f') m_content_length = (m_content_length << 4) + (c - 'a') + 10;
          else if ('A' <= c && c <= 'F') m_content_length = (m_content_length << 4) + (c - 'A') + 10;
          break;

        // Read chunk ending
        case CHUNK_TAIL:
          if (c == '\n') m_state = CHUNK_HEAD;
          break;

        // Read the last chunk ending
        case CHUNK_TAIL_LAST:
          if (c == '\n') {
            end_message(ctx);
            m_state = METHOD;
            m_name.clear();
          }
          break;
        }
        return false;

      }, *read);

      if (is_body && !read->empty()) output(read);
      if (is_end) {
        end_message(ctx);
      }
    }

  // End of session
  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
    output(inp);
  }
}

bool RequestDecoder::is_keep_alive() {
  if (!m_connection.empty()) {
    if (!strncmp(m_connection.c_str(), "close", 5)) {
      return false;
    }
  } else if (m_keep_alive.empty()) {
    if (!strcasecmp(m_protocol.c_str(), "HTTP/1.0")) {
      return false;
    }
  }
  return true;
}

void RequestDecoder::end_message(Context *ctx) {
  if (auto inbound = ctx->inbound()) {
    inbound->set_keep_alive_request(is_keep_alive());
    inbound->increase_request_count();
  }
  output(MessageEnd::make());
}

//
// ResponseDecoder
//

ResponseDecoder::ResponseDecoder(bool bodiless)
  : m_bodiless(bodiless)
{
}

ResponseDecoder::ResponseDecoder(const ResponseDecoder &r)
  : ResponseDecoder(r.m_bodiless)
{
}

ResponseDecoder::~ResponseDecoder()
{
}

auto ResponseDecoder::help() -> std::list<std::string> {
  if (m_bodiless) {
    return {
      "decodeHttpBodilessResponse()",
      "Deframes an HTTP response message without a body (response to a HEAD request)",
    };
  } else {
    return {
      "decodeHttpResponse()",
      "Deframes an HTTP response message",
    };
  }
}

void ResponseDecoder::dump(std::ostream &out) {
  if (m_bodiless) {
    out << "decodeHttpBodilessResponse";
  } else {
    out << "decodeHttpResponse";
  }
}

void ResponseDecoder::reset() {
  m_state = PROTOCOL;
  m_name.clear();
  m_head = nullptr;
  m_session_end = false;
}

auto ResponseDecoder::clone() -> Filter* {
  return new ResponseDecoder(*this);
}

void ResponseDecoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  // Data
  if (auto data = inp->as<Data>()) {
    while (!data->empty()) {
      auto is_body = (m_state == BODY);
      auto is_end = false;

      // Read up to body data boundaries
      pjs::Ref<Data> read(Data::make());
      data->shift_to([&](int c) {

        // Parse one character
        switch (m_state) {

        // Read protocol
        case PROTOCOL:
          if (c == ' ') {
            m_head = ResponseHead::make();
            m_head->headers(pjs::Object::make());
            m_head->protocol(pjs::Str::make(m_name.str()));
            m_state = STATUS_CODE;
            m_name.clear();
          } else {
            m_name.push(c);
          }
          break;

        // Read status code
        case STATUS_CODE:
          if (c == ' ') {
            m_head->status(std::atoi(m_name.c_str()));
            m_state = STATUS;
            m_name.clear();
          } else {
            m_name.push(c);
          }
          break;

        // Read status
        case STATUS:
          if (c == '\n') {
            m_head->status_text(pjs::Str::make(m_name.str()));
            m_state = HEADER_NAME;
            m_name.clear();
            m_transfer_encoding.clear();
            m_chunked = false;
            m_content_length = 0;
          } else {
            m_name.push(c);
          }
          break;

        // Read header name
        case HEADER_NAME:
          if (c == ':') {
            m_state = HEADER_VALUE;
            m_value.clear();
          } else if (c == '\n' && m_name.empty()) {
            output(MessageStart::make(m_head));
            parse_transfer_encoding(m_transfer_encoding, m_content_length, m_chunked);
            if (m_chunked) {
              m_state = CHUNK_HEAD;
            } else if (m_content_length > 0) {
              m_state = BODY;
              return true;
            } else {
              output(MessageEnd::make());
              m_state = PROTOCOL;
            }
          } else {
            m_name.push(c);
          }
          break;

        // Read header value
        case HEADER_VALUE:
          if (c == '\n') {
            auto name = m_name.str();
            for (auto &ch : name) ch = std::tolower(ch);
            if (name == "content-length") m_content_length = std::atoi(m_value.str().c_str());
            else if (name == "transfer-encoding") m_transfer_encoding = m_value.str();
            else if (auto headers = m_head->as<ResponseHead>()->headers()) {
              pjs::Ref<pjs::Str> key(pjs::Str::make(name));
              headers->ht_set(key, m_value.str());
            }
            m_state = HEADER_NAME;
            m_name.clear();
          } else {
            m_value.push(c);
          }
          break;

        // Read body
        case BODY:
          if (!--m_content_length) {
            if (m_chunked) {
              m_state = CHUNK_TAIL;
            } else {
              m_state = PROTOCOL;
              is_end = true;
            }
            return true;
          }
          break;

        // Read chunk length
        case CHUNK_HEAD:
          if (c == '\n') {
            if (m_content_length > 0) {
              m_state = BODY;
              return true;
            } else {
              m_state = CHUNK_TAIL_LAST;
            }
          }
          else if ('0' <= c && c <= '9') m_content_length = (m_content_length << 4) + (c - '0');
          else if ('a' <= c && c <= 'f') m_content_length = (m_content_length << 4) + (c - 'a') + 10;
          else if ('A' <= c && c <= 'F') m_content_length = (m_content_length << 4) + (c - 'A') + 10;
          break;

        // Read chunk ending
        case CHUNK_TAIL:
          if (c == '\n') m_state = CHUNK_HEAD;
          break;

        // Read the last chunk ending
        case CHUNK_TAIL_LAST:
          if (c == '\n') {
            output(MessageEnd::make());
            m_state = PROTOCOL;
          }
        }
        return false;

      }, *read);

      if (is_body && !read->empty()) output(read);
      if (is_end) output(MessageEnd::make());
    }

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
      head->protocol(pjs::Str::make(m_name.str()));
      head->status(status_code);
      head->status_text(pjs::Str::make(status_text));
      output(MessageStart::make(head));
      output(MessageEnd::make());
      output(SessionEnd::make());
    } else {
      output(inp);
    }
  }
}

//
// RequestEncoder
//

std::string RequestEncoder::s_default_protocol("HTTP/1.1");
std::string RequestEncoder::s_default_method("GET");
std::string RequestEncoder::s_default_path("/");
std::string RequestEncoder::s_header_content_length("Content-Length: ");

RequestEncoder::RequestEncoder()
{
}

RequestEncoder::RequestEncoder(pjs::Object *head)
  : m_head(head)
  , m_prop_protocol("protocol")
  , m_prop_method("method")
  , m_prop_path("path")
  , m_prop_headers("headers")
{
}

RequestEncoder::RequestEncoder(const RequestEncoder &r)
  : RequestEncoder(r.m_head)
{
}

RequestEncoder::~RequestEncoder()
{
}

auto RequestEncoder::help() -> std::list<std::string> {
  return {
    "encodeHttpRequest([head])",
    "Frames an HTTP request message",
    "head = <object|function> Request head including protocol, method, path, headers",
  };
}

void RequestEncoder::dump(std::ostream &out) {
  out << "encodeHttpRequest";
}

auto RequestEncoder::clone() -> Filter* {
  return new RequestEncoder(*this);
}

void RequestEncoder::reset() {
  m_message_start = nullptr;
  m_buffer = nullptr;
  m_session_end = false;
}

void RequestEncoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (inp->is<MessageStart>()) {
    m_message_start = inp->as<MessageStart>();
    m_buffer = Data::make();

  } else if (inp->is<MessageEnd>()) {
    if (!m_message_start) return;

    pjs::Value head_obj(m_head), head;
    if (!eval(*ctx, head_obj, head)) return;
    if (!head.is_object() || head.is_null()) head.set(m_message_start->head());

    pjs::Str *str_protocol = nullptr;
    pjs::Str *str_method = nullptr;
    pjs::Str *str_path = nullptr;
    pjs::Object *obj_headers = nullptr;

    if (head.is_object()) {
      if (auto obj = head.o()) {
        pjs::Value protocol, method, path, headers;
        m_prop_protocol.get(obj, protocol);
        m_prop_method.get(obj, method);
        m_prop_path.get(obj, path);
        m_prop_headers.get(obj, headers);
        if (protocol.to_boolean()) str_protocol = protocol.to_string();
        if (method.to_boolean()) str_method = method.to_string();
        if (path.to_boolean()) str_path = path.to_string();
        if (headers.is_object()) obj_headers = headers.o()->retain();
      }
    }

    auto header_data = Data::make();
    header_data->push(str_method ? str_method->str() : s_default_method);
    header_data->push(' ');
    header_data->push(str_path ? str_path->str() : s_default_path);
    header_data->push(' ');
    header_data->push(str_protocol ? str_protocol->str() : s_default_protocol);
    header_data->push("\r\n");

    if (obj_headers) {
      obj_headers->iterate_all([&](pjs::Str *key, const pjs::Value &val) {
        if (val.is_nullish()) return;
        auto s = val.to_string();
        if (!is_content_length(s->str())) {
          header_data->push(key->str());
          header_data->push(": ");
          header_data->push(s->str());
          header_data->push("\r\n");
        }
        s->release();
      });
    }

    header_data->push(s_header_content_length);
    header_data->push(std::to_string(m_buffer->size()));
    header_data->push("\r\n\r\n");

    if (str_protocol) str_protocol->release();
    if (str_method) str_method->release();
    if (str_path) str_path->release();
    if (obj_headers) obj_headers->release();

    output(m_message_start);
    output(header_data);
    output(m_buffer);
    output(inp);

    m_message_start = nullptr;
    m_buffer = nullptr;

  } else if (auto data = inp->as<Data>()) {
    if (m_buffer) m_buffer->push(*data);

  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
    output(inp);
  }
}

//
// ResponseEncoder
//

std::string ResponseEncoder::s_default_protocol("HTTP/1.1");
std::string ResponseEncoder::s_default_status("200");
std::string ResponseEncoder::s_default_status_text("OK");
std::string ResponseEncoder::s_header_connection_keep_alive("Connection: keep-alive\r\n");
std::string ResponseEncoder::s_header_connection_close("Connection: close\r\n");
std::string ResponseEncoder::s_header_content_length("Content-Length: ");

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

ResponseEncoder::ResponseEncoder()
{
}

ResponseEncoder::ResponseEncoder(pjs::Object *head)
  : m_head(head)
  , m_prop_protocol("protocol")
  , m_prop_status("status")
  , m_prop_status_text("statusText")
  , m_prop_headers("headers")
  , m_prop_bodiless("bodiless")
{
}

ResponseEncoder::ResponseEncoder(const ResponseEncoder &r)
  : ResponseEncoder(r.m_head)
{
}

ResponseEncoder::~ResponseEncoder()
{
}

auto ResponseEncoder::help() -> std::list<std::string> {
  return {
    "encodeHttpResponse([head])",
    "Frames an HTTP response message",
    "head = <object|function> Response head including protocol, status, statusText, headers",
  };
}

void ResponseEncoder::dump(std::ostream &out) {
  out << "encodeHttpResponse";
}

auto ResponseEncoder::clone() -> Filter* {
  return new ResponseEncoder(*this);
}

void ResponseEncoder::reset() {
  m_message_start = nullptr;
  m_buffer = nullptr;
  m_session_end = false;
}

void ResponseEncoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (inp->is<MessageStart>()) {
    m_message_start = inp;
    m_buffer = Data::make();

  } else if (inp->is<MessageEnd>()) {
    if (!m_message_start) return;

    bool keep_alive = true;
    if (auto inbound = ctx->inbound()) {
      keep_alive = inbound->increase_response_count();
    }

    pjs::Value head_obj(m_head), head;
    if (!eval(*ctx, head_obj, head)) return;
    if (!head.is_object() || head.is_null()) head.set(m_message_start->head());

    pjs::Str *str_protocol = nullptr;
    int num_status = 0;
    pjs::Str *str_status_text = nullptr;
    pjs::Object *obj_headers = nullptr;
    bool is_bodiless = false;

    if (head.is_object()) {
      if (auto obj = head.o()) {
        pjs::Value protocol, status, status_text, headers, bodiless;
        m_prop_protocol.get(obj, protocol);
        m_prop_status.get(obj, status);
        m_prop_status_text.get(obj, status_text);
        m_prop_headers.get(obj, headers);
        m_prop_bodiless.get(obj, bodiless);
        if (protocol.to_boolean()) str_protocol = protocol.to_string();
        if (status.to_boolean()) num_status = status.to_number();
        if (status_text.to_boolean()) str_status_text = status_text.to_string();
        if (headers.is_object()) obj_headers = headers.o()->retain();
        is_bodiless = bodiless.to_boolean();
      }
    }

    auto header_data = Data::make();
    header_data->push(str_protocol ? str_protocol->str() : s_default_protocol);
    header_data->push(' ');
    header_data->push(num_status ? std::to_string(num_status) : s_default_status);
    header_data->push(' ');
    if (str_status_text) header_data->push(str_status_text->str());
    else {
      const auto *status_name = lookup_status_text(num_status);
      header_data->push(status_name ? std::string(status_name) : s_default_status_text);
    }
    header_data->push("\r\n");

    if (obj_headers) {
      obj_headers->iterate_all([&](pjs::Str *key, const pjs::Value &val) {
        if (val.is_nullish()) return;
        auto s = val.to_string();
        if (is_bodiless || !is_content_length(s->str())) {
          header_data->push(key->str());
          header_data->push(": ");
          header_data->push(s->str());
          header_data->push("\r\n");
        }
        s->release();
      });
    }

    if (keep_alive) {
      header_data->push(s_header_connection_keep_alive);
    } else {
      header_data->push(s_header_connection_close);
    }

    if (!is_bodiless) {
      header_data->push(s_header_content_length);
      header_data->push(std::to_string(m_buffer->size()));
      header_data->push("\r\n");
    }

    header_data->push("\r\n");

    if (str_protocol) str_protocol->release();
    if (str_status_text) str_status_text->release();
    if (obj_headers) obj_headers->release();

    output(m_message_start);
    output(header_data);
    if (!is_bodiless) output(m_buffer);
    output(inp);

    if (!keep_alive) output(SessionEnd::make());

    m_message_start = nullptr;
    m_buffer = nullptr;

  } else if (auto data = inp->as<Data>()) {
    if (m_buffer) m_buffer->push(*data);

  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
    output(inp);
  }
}

} // namespace http
} // namespace pipy