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

RequestDecoder::RequestDecoder(pjs::Object *options) {
  if (options) {
    options->get("decompress", m_decompress);
  }
}

RequestDecoder::RequestDecoder(const RequestDecoder &r)
  : m_decompress(r.m_decompress)
{
}

RequestDecoder::~RequestDecoder()
{
}

auto RequestDecoder::help() -> std::list<std::string> {
  return {
    "decodeHttpRequest([options])",
    "Deframes an HTTP request message",
    "options = <object> Options including decompress"
  };
}

void RequestDecoder::dump(std::ostream &out) {
  out << "decodeHttpRequest";
}

auto RequestDecoder::clone() -> Filter* {
  return new RequestDecoder(*this);
}

void RequestDecoder::reset() {
  if (m_decompressor) {
    m_decompressor->end();
    m_decompressor = nullptr;
  }
  m_state = METHOD;
  m_name.clear();
  m_head = nullptr;
  m_connected = false;
  m_session_end = false;
}

void RequestDecoder::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  // Data
  if (auto data = inp->as<Data>()) {
    if (m_connected) {
      output(data);
      return;
    }

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
            m_content_encoding.clear();
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
            if (!m_chunked && m_content_length <= 0) {
              end_message(ctx);
              m_state = METHOD;
              m_name.clear();
              if (m_connected) {
                output(MessageStart::make());
                return true;
              }
            } else {
              if (m_content_encoding == "gzip") {
                pjs::Value ret;
                eval(*ctx, m_decompress, ret);
                if (ret.to_boolean()) {
                  m_decompressor = Decompressor::inflate(out());
                }
              }
              if (m_chunked) {
                m_state = CHUNK_HEAD;
              } else {
                m_state = BODY;
                return true;
              }
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
            if (name == "content-encoding") m_content_encoding = m_value.str();
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
            if (m_connected) {
              output(MessageStart::make());
              return true;
            }
          }
          break;
        }
        return false;

      }, *read);

      if (is_body && !read->empty()) {
        if (m_decompressor) {
          m_decompressor->process(read);
        } else {
          output(read);
        }
      }

      if (is_end) {
        if (m_decompressor) {
          m_decompressor->end();
          m_decompressor = nullptr;
        }
        end_message(ctx);
      }
    }

  // End of session
  } else if (inp->is<SessionEnd>()) {
    if (m_connected) output(MessageEnd::make());
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
  static pjs::Ref<pjs::Str> CONNECT(pjs::Str::make("CONNECT"));
  if (m_head->method() == CONNECT.get()) {
    m_connected = true;
  }
  if (auto inbound = ctx->inbound()) {
    inbound->set_keep_alive_request(is_keep_alive());
    inbound->increase_request_count();
  }
  output(MessageEnd::make());
}

//
// ResponseDecoder
//

ResponseDecoder::ResponseDecoder()
{
}

ResponseDecoder::ResponseDecoder(pjs::Object *options) {
  if (options) {
    options->get("bodiless", m_bodiless);
    options->get("decompress", m_decompress);
  }
}

ResponseDecoder::ResponseDecoder(const std::function<bool()> &bodiless)
  : m_bodiless_func(bodiless)
{
}

ResponseDecoder::ResponseDecoder(const ResponseDecoder &r)
  : m_bodiless(r.m_bodiless)
  , m_bodiless_func(r.m_bodiless_func)
  , m_decompress(r.m_decompress)
{
}

ResponseDecoder::~ResponseDecoder()
{
}

auto ResponseDecoder::help() -> std::list<std::string> {
  return {
    "decodeHttpResponse([options])",
    "Deframes an HTTP response message",
    "options = <object> Options including bodiless and decompress"
  };
}

void ResponseDecoder::dump(std::ostream &out) {
  out << "decodeHttpResponse";
}

void ResponseDecoder::reset() {
  if (m_decompressor) {
    m_decompressor->end();
    m_decompressor = nullptr;
  }
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
            m_content_encoding.clear();
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
            if (is_bodiless(ctx)) {
              output(MessageEnd::make());
              m_state = PROTOCOL;
            } else if (!m_chunked && m_content_length <= 0) {
              output(MessageEnd::make());
              m_state = PROTOCOL;
            } else {
              if (m_content_encoding == "gzip") {
                pjs::Value ret;
                eval(*ctx, m_decompress, ret);
                if (ret.to_boolean()) {
                  m_decompressor = Decompressor::inflate(out());
                }
              }
              if (m_chunked) {
                m_state = CHUNK_HEAD;
              } else {
                m_state = BODY;
                return true;
              }
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
            if (name == "content-encoding") m_content_encoding = m_value.str();
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
            if (m_decompressor) {
              m_decompressor->end();
              m_decompressor = nullptr;
            }
            output(MessageEnd::make());
            m_state = PROTOCOL;
          }
        }
        return false;

      }, *read);

      if (is_body && !read->empty()) {
        if (m_decompressor) {
          m_decompressor->process(read);
        } else {
          output(read);
        }
      }

      if (is_end) {
        if (m_decompressor) {
          m_decompressor->end();
          m_decompressor = nullptr;
        }
        output(MessageEnd::make());
      }
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
  static Data::Producer s_dp("encodeHttpRequest");

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
    s_dp.push(header_data, str_method ? str_method->str() : s_default_method);
    s_dp.push(header_data, ' ');
    s_dp.push(header_data, str_path ? str_path->str() : s_default_path);
    s_dp.push(header_data, ' ');
    s_dp.push(header_data, str_protocol ? str_protocol->str() : s_default_protocol);
    s_dp.push(header_data, "\r\n");

    if (obj_headers) {
      obj_headers->iterate_all([&](pjs::Str *key, const pjs::Value &val) {
        if (val.is_nullish()) return;
        auto s = val.to_string();
        if (!is_content_length(s->str())) {
          s_dp.push(header_data, key->str());
          s_dp.push(header_data, ": ");
          s_dp.push(header_data, s->str());
          s_dp.push(header_data, "\r\n");
        }
        s->release();
      });
    }

    s_dp.push(header_data, s_header_content_length);
    s_dp.push(header_data, std::to_string(m_buffer->size()));
    s_dp.push(header_data, "\r\n\r\n");

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
  static Data::Producer s_dp("encodeHttpResponse");

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
    s_dp.push(header_data, str_protocol ? str_protocol->str() : s_default_protocol);
    s_dp.push(header_data, ' ');
    s_dp.push(header_data, num_status ? std::to_string(num_status) : s_default_status);
    s_dp.push(header_data, ' ');
    if (str_status_text) s_dp.push(header_data, str_status_text->str());
    else {
      const auto *status_name = lookup_status_text(num_status);
      s_dp.push(header_data, status_name ? std::string(status_name) : s_default_status_text);
    }
    s_dp.push(header_data, "\r\n");

    if (obj_headers) {
      obj_headers->iterate_all([&](pjs::Str *key, const pjs::Value &val) {
        if (val.is_nullish()) return;
        auto s = val.to_string();
        if (is_bodiless || !is_content_length(s->str())) {
          s_dp.push(header_data, key->str());
          s_dp.push(header_data, ": ");
          s_dp.push(header_data, s->str());
          s_dp.push(header_data, "\r\n");
        }
        s->release();
      });
    }

    if (keep_alive) {
      s_dp.push(header_data, s_header_connection_keep_alive);
    } else {
      s_dp.push(header_data, s_header_connection_close);
    }

    if (!is_bodiless) {
      s_dp.push(header_data, s_header_content_length);
      s_dp.push(header_data, std::to_string(m_buffer->size()));
      s_dp.push(header_data, "\r\n");
    }

    s_dp.push(header_data, "\r\n");

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

static const pjs::Ref<pjs::Str> s_protocol(pjs::Str::make("protocol"));
static const pjs::Ref<pjs::Str> s_method(pjs::Str::make("method"));
static const pjs::Ref<pjs::Str> s_head(pjs::Str::make("HEAD"));
static const pjs::Ref<pjs::Str> s_path(pjs::Str::make("path"));
static const pjs::Ref<pjs::Str> s_status(pjs::Str::make("status"));
static const pjs::Ref<pjs::Str> s_status_text(pjs::Str::make("statusText"));
static const pjs::Ref<pjs::Str> s_headers(pjs::Str::make("headers"));
static const pjs::Ref<pjs::Str> s_http_1_0(pjs::Str::make("HTTP/1.0"));
static const pjs::Ref<pjs::Str> s_connection(pjs::Str::make("connection"));
static const pjs::Ref<pjs::Str> s_keep_alive(pjs::Str::make("keep-alive"));
static const pjs::Ref<pjs::Str> s_close(pjs::Str::make("close"));
static const pjs::Ref<pjs::Str> s_transfer_encoding(pjs::Str::make("transfer-encoding"));
static const pjs::Ref<pjs::Str> s_content_length(pjs::Str::make("content-length"));
static const pjs::Ref<pjs::Str> s_content_encoding(pjs::Str::make("content-encoding"));

//
// Decoder
//

class Decoder {
public:
  Decoder(bool is_response, const Event::Receiver &output)
    : m_output(output)
    , m_is_response(is_response) {}

  void reset() {
    m_state = HEAD;
    m_head_buffer.clear();
    m_head = nullptr;
    m_body_size = 0;
    m_is_bodiless = false;
    m_is_final = false;
  }

  void input(const pjs::Ref<Data> &data);

  bool is_bodiless() const { return m_is_bodiless; }
  bool is_final() const { return m_is_final; }

  void set_bodiless(bool b) { m_is_bodiless = b; }

private:
  const static int MAX_HEADER_SIZE = 0x1000;

  enum State {
    HEAD,
    HEAD_EOL,
    HEADER,
    HEADER_EOL,
    BODY,
    CHUNK_HEAD,
    CHUNK_BODY,
    CHUNK_TAIL,
    CHUNK_LAST,
  };

  Event::Receiver m_output;
  State m_state = HEAD;
  Data m_head_buffer;
  pjs::Ref<MessageHead> m_head;
  int m_body_size = 0;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_final = false;

  void output_start() {
    m_output(MessageStart::make(m_head));
  }

  void output_end() {
    m_output(MessageEnd::make());
    m_is_bodiless = false;
    m_is_final = false;
  }
};

void Decoder::input(const pjs::Ref<Data> &data) {
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
          headers->ht_delete(s_content_length);

          // Connection and Keep-Alive
          if (connection.is_string()) {
            m_is_final = (connection.s() == s_close);
          } else {
            m_is_final = (m_head->protocol() == s_http_1_0);
          }

          // Transfer-Encoding and Content-Length
          if (transfer_encoding.is_string() && !strncmp(transfer_encoding.s()->c_str(), "chunked", 7)) {
            output_start();
            if (m_is_response && m_is_bodiless) {
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
              output_start();
              if (m_is_response && m_is_bodiless) {
                output_end();
                state = HEAD;
              } else {
                state = BODY;
              }
            } else {
              output_start();
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

//
// Encoder
//

class Encoder {
public:
  Encoder(bool is_response, const Event::Receiver &output)
    : m_output(output)
    , m_is_response(is_response) {}

  void reset() {
    m_start = nullptr;
    m_buffer = nullptr;
    m_is_bodiless = false;
    m_is_final = false;
  }

  void input(const pjs::Ref<Event> &evt);

  bool is_bodiless() const { return m_is_bodiless; }

  void set_bodiless(bool b) { m_is_bodiless = b; }
  void set_final(bool b) { m_is_final = b; }

private:
  Event::Receiver m_output;
  pjs::Ref<MessageStart> m_start;
  pjs::Ref<Data> m_buffer;
  int m_content_length = -1;
  bool m_chunked = false;
  bool m_is_response;
  bool m_is_bodiless = false;
  bool m_is_final = false;

  void output_head();

  static Data::Producer s_dp;
};

Data::Producer Encoder::s_dp("HTTP Encoder");

void Encoder::input(const pjs::Ref<Event> &evt) {
  if (auto start = evt->as<MessageStart>()) {
    m_start = start;
    m_buffer = nullptr;
    m_content_length = -1;
    m_chunked = false;

    if (auto head = start->head()) {
      pjs::Value headers, transfer_encoding, content_length;
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

    if (m_chunked || m_content_length >= 0) {
      output_head();
    } else {
      m_buffer = Data::make();
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      if (!m_is_response || !m_is_bodiless) {
        if (m_buffer) {
          m_buffer->push(*data);
        } else {
          if (m_chunked && !data->empty()) {
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
      }
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      if (!m_is_response || !m_is_bodiless) {
        if (m_chunked) {
          m_output(s_dp.make("0\r\n\r\n"));
        } else if (m_buffer) {
          m_content_length = m_buffer->size();
          output_head();
          if (!m_buffer->empty()) m_output(m_buffer);
        }
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
      m_is_bodiless = (method.s() == s_head);
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
        s_dp.push(buffer, k->str());
        s_dp.push(buffer, ": ");
        if (k == s_content_length) {
          if (!m_chunked) {
            char str[100];
            std::sprintf(str, "%d\r\n", m_content_length);
            s_dp.push(buffer, str);
            content_length_written = true;
          }
        } else if (k != s_connection && k != s_keep_alive) {
          auto s = v.to_string();
          s_dp.push(buffer, s->str());
          s_dp.push(buffer, "\r\n");
          s->release();
        }
      }
    );
  }

  if (!content_length_written && !m_chunked && m_content_length > 0) {
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

Demux::Demux(pjs::Str *target)
  : m_target(target)
{
}

Demux::Demux(const Demux &r)
  : m_target(r.m_target)
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
    auto mod = pipeline()->module();
    auto pipeline = mod->find_named_pipeline(m_target);
    if (!pipeline) {
      Log::error("[demux] unknown pipeline: %s", m_target->c_str());
      abort();
      return;
    }

    auto worker = mod->worker();

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
      m_decoder.input(data);
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
        if (evt->is<MessageStart>()) {
          m_decoder.set_bodiless(stream->m_bodiless);
        } else if (evt->is<MessageEnd>()) {
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

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::new_connection() -> Connection* {
  return new ClientConnection;
}

} // namespace http
} // namespace pipy