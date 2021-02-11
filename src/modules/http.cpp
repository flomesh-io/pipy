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
#include "logging.hpp"
#include "utils.hpp"

#include <algorithm>

NS_BEGIN

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

  static void write_headers_from_context(Data &out, const Context &ctx, const std::string &prefix) {
    static const char CL[] = { "content-length" };
    auto s = prefix.c_str();
    auto n = prefix.length();
    for (const auto &p : ctx.variables) {
      const auto &k = p.first;
      if (k.length() > n && !std::strncmp(k.c_str(), s, n)) {
        auto is_content_length = true;
        for (size_t i = n, j = 0; i < k.length(); i++, j++) {
          if (std::tolower(k[i]) != CL[j]) {
            is_content_length = false;
            break;
          }
        }
        if (!is_content_length) {
          out.push(k.c_str() + n, k.length() - n);
          out.push(": ");
          out.push(p.second);
          out.push("\r\n");
        }
      }
    }
  }

  //
  // RequestDecoder
  //

  RequestDecoder::RequestDecoder() {
  }

  RequestDecoder::~RequestDecoder() {
  }

  auto RequestDecoder::help() -> std::list<std::string> {
    return {
      "Deframes an HTTP request message and outputs its body",
      "prefix = Context prefix for message info",
    };
  }

  void RequestDecoder::config(const std::map<std::string, std::string> &params) {
    auto prefix = utils::get_param(params, "prefix");
    m_var_protocol = prefix + ".protocol";
    m_var_method = prefix + ".method";
    m_var_path = prefix + ".path";
    m_var_headers = prefix + ".request.";
  }

  auto RequestDecoder::clone() -> Module* {
    auto clone = new RequestDecoder();
    clone->m_var_protocol = m_var_protocol;
    clone->m_var_method = m_var_method;
    clone->m_var_path = m_var_path;
    clone->m_var_headers = m_var_headers;
    return clone;
  }

  void RequestDecoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Session start.
    if (obj->is<SessionStart>()) {
      m_state = METHOD;
      m_name.clear();
      m_message_count = 0;
      out(std::move(obj));

    // Data.
    } else if (auto data = obj->as<Data>()) {
      while (!data->empty()) {
        auto is_body = (m_state == BODY);
        auto is_break = false;
        auto is_end = false;

        // Read up to body data boundaries.
        auto read = data->shift_until([&](int c) {
          if (is_break) return true;

          // Parse one character.
          switch (m_state) {

          // Read method.
          case METHOD:
            if (c == ' ') {
              if (!m_var_method.empty()) ctx->variables[m_var_method] = m_name.str();
              m_state = PATH;
              m_name.clear();
            } else {
              m_name.push(c);
            }
            break;

          // Read path.
          case PATH:
            if (c == ' ') {
              if (!m_var_path.empty()) ctx->variables[m_var_path] = m_name.str();
              m_state = PROTOCOL;
              m_name.clear();
            } else {
              m_name.push(c);
            }
            break;

          // Read protocol.
          case PROTOCOL:
            if (c == '\n') {
              if (!m_var_protocol.empty()) ctx->variables[m_var_protocol] = m_name.str();
              m_state = HEADER_NAME;
              m_name.clear();
              m_transfer_encoding.clear();
              m_chunked = false;
              m_content_length = 0;
            } else {
              m_name.push(c);
            }
            break;

          // Read header name.
          case HEADER_NAME:
            if (c == ':') {
              m_state = HEADER_VALUE;
              m_value.clear();
            } else if (c == '\n' && m_name.empty()) {
              m_message_count++;
              out(make_object<MessageStart>());
              parse_transfer_encoding(m_transfer_encoding, m_content_length, m_chunked);
              if (m_chunked) {
                m_state = CHUNK_HEAD;
              } else if (m_content_length > 0) {
                m_state = BODY;
                is_break = true;
              } else {
                m_state = METHOD;
                m_name.clear();
                out(make_object<MessageEnd>());
              }
            } else {
              m_name.push(c);
            }
            break;

          // Read header value.
          case HEADER_VALUE:
            if (c == '\n') {
              auto name = m_name.str();
              for (auto &ch : name) ch = std::tolower(ch);
              if (name == "content-length") m_content_length = std::atoi(m_value.str().c_str());
              else if (name == "transfer-encoding") m_transfer_encoding = m_value.str();
              if (!m_var_headers.empty()) ctx->variables[m_var_headers + name] = m_value.str();
              m_state = HEADER_NAME;
              m_name.clear();
            } else {
              m_value.push(c);
            }
            break;

          // Read body.
          case BODY:
            if (!--m_content_length) {
              if (m_chunked) {
                m_state = CHUNK_TAIL;
                is_break = true;
              } else {
                m_state = METHOD;
                m_name.clear();
                is_break = true;
                is_end = true;
              }
            }
            break;

          // Read chunk length.
          case CHUNK_HEAD:
            if (c == '\n') {
              if (m_content_length > 0) {
                m_state = BODY;
                is_break = true;
              } else {
                m_state = CHUNK_TAIL_LAST;
              }
            }
            else if ('0' <= c && c <= '9') m_content_length = (m_content_length << 4) + (c - '0');
            else if ('a' <= c && c <= 'f') m_content_length = (m_content_length << 4) + (c - 'a') + 10;
            else if ('A' <= c && c <= 'F') m_content_length = (m_content_length << 4) + (c - 'A') + 10;
            break;

          // Read chunk ending.
          case CHUNK_TAIL:
            if (c == '\n') m_state = CHUNK_HEAD;
            break;

          // Read the last chunk ending
          case CHUNK_TAIL_LAST:
            if (c == '\n') {
              out(make_object<MessageEnd>());
              m_state = METHOD;
              m_name.clear();
            }
            break;
          }
          return false;
        });

        if (is_body && !read.empty()) {
          out(make_object<Data>(std::move(read)));
        }

        if (is_end) {
          out(make_object<MessageEnd>());
        }
      }

    // End of session.
    } else if (obj->is<SessionEnd>()) {
      if (m_state == METHOD && m_name.length() == 0) {
        if (m_message_count == 0) {
          Log::warn("[http] empty request, downstream peer: %s", ctx->remote_addr.c_str());
        }
      } else {
        Log::warn("[http] incomplete request message, downstream peer: %s", ctx->remote_addr.c_str());
      }
      out(std::move(obj));

    // Pass all the other objects.
    } else {
      out(std::move(obj));
    }
  }

  //
  // ResponseDecoder
  //

  ResponseDecoder::ResponseDecoder() {
  }

  ResponseDecoder::~ResponseDecoder() {
  }

  auto ResponseDecoder::help() -> std::list<std::string> {
    return {
      "Deframes an HTTP response message and outputs its body",
      "prefix = Context prefix for message info",
    };
  }

  void ResponseDecoder::config(const std::map<std::string, std::string> &params) {
    auto prefix = utils::get_param(params, "prefix");
    m_var_protocol = prefix + ".protocol";
    m_var_status_code = prefix + ".status_code";
    m_var_status = prefix + ".status";
    m_var_headers = prefix + ".response.";
  }

  auto ResponseDecoder::clone() -> Module* {
    auto clone = new ResponseDecoder();
    clone->m_var_protocol = m_var_protocol;
    clone->m_var_status_code = m_var_status_code;
    clone->m_var_status = m_var_status;
    clone->m_var_headers = m_var_headers;
    return clone;
  }

  void ResponseDecoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Session start.
    if (obj->is<SessionStart>()) {
      m_state = PROTOCOL;
      m_name.clear();
      m_message_count = 0;
      out(std::move(obj));

    // Data.
    } else if (auto data = obj->as<Data>()) {
      while (!data->empty()) {
        auto is_body = (m_state == BODY);
        auto is_break = false;
        auto is_end = false;

        // Read up to body data boundaries.
        auto read = data->shift_until([&](int c) {
          if (is_break) return true;

          // Parse one character.
          switch (m_state) {

          // Read protocol.
          case PROTOCOL:
            if (c == ' ') {
              if (!m_var_protocol.empty()) ctx->variables[m_var_protocol] = m_name.str();
              m_state = STATUS_CODE;
              m_name.clear();
            } else {
              m_name.push(c);
            }
            break;

          // Read status code.
          case STATUS_CODE:
            if (c == ' ') {
              if (!m_var_status_code.empty()) ctx->variables[m_var_status_code] = m_name.str();
              m_state = STATUS;
              m_name.clear();
            } else {
              m_name.push(c);
            }
            break;

          // Read status.
          case STATUS:
            if (c == '\n') {
              if (!m_var_status.empty()) ctx->variables[m_var_status] = m_name.str();
              m_state = HEADER_NAME;
              m_name.clear();
              m_transfer_encoding.clear();
              m_chunked = false;
              m_content_length = 0;
            } else {
              m_name.push(c);
            }
            break;

          // Read header name.
          case HEADER_NAME:
            if (c == ':') {
              m_state = HEADER_VALUE;
              m_value.clear();
            } else if (c == '\n' && m_name.empty()) {
              m_message_count++;
              out(make_object<MessageStart>());
              parse_transfer_encoding(m_transfer_encoding, m_content_length, m_chunked);
              if (m_chunked) {
                m_state = CHUNK_HEAD;
              } else if (m_content_length > 0) {
                m_state = BODY;
                is_break = true;
              } else {
                out(make_object<MessageEnd>());
                m_state = PROTOCOL;
              }
            } else {
              m_name.push(c);
            }
            break;

          // Read header value.
          case HEADER_VALUE:
            if (c == '\n') {
              auto name = m_name.str();
              for (auto &ch : name) ch = std::tolower(ch);
              if (name == "content-length") m_content_length = std::atoi(m_value.str().c_str());
              else if (name == "transfer-encoding") m_transfer_encoding = m_value.str();
              if (!m_var_headers.empty()) ctx->variables[m_var_headers + name] = m_value.str();
              m_state = HEADER_NAME;
              m_name.clear();
            } else {
              m_value.push(c);
            }
            break;

          // Read body.
          case BODY:
            if (!--m_content_length) {
              if (m_chunked) {
                m_state = CHUNK_TAIL;
                is_break = true;
              } else {
                m_state = PROTOCOL;
                is_break = true;
                is_end = true;
              }
            }
            break;

          // Read chunk length.
          case CHUNK_HEAD:
            if (c == '\n') {
              if (m_content_length > 0) {
                m_state = BODY;
                is_break = true;
              } else {
                m_state = CHUNK_TAIL_LAST;
              }
            }
            else if ('0' <= c && c <= '9') m_content_length = (m_content_length << 4) + (c - '0');
            else if ('a' <= c && c <= 'f') m_content_length = (m_content_length << 4) + (c - 'a') + 10;
            else if ('A' <= c && c <= 'F') m_content_length = (m_content_length << 4) + (c - 'A') + 10;
            break;

          // Read chunk ending.
          case CHUNK_TAIL:
            if (c == '\n') m_state = CHUNK_HEAD;
            break;

          // Read the last chunk ending.
          case CHUNK_TAIL_LAST:
            if (c == '\n') {
              out(make_object<MessageEnd>());
              m_state = PROTOCOL;
            }
          }
          return false;
        });

        if (is_body && !read.empty()) {
          out(make_object<Data>(std::move(read)));
        }

        if (is_end) {
          out(make_object<MessageEnd>());
        }
      }

    // End of session.
    } else if (obj->is<SessionEnd>()) {
      if (m_state == PROTOCOL && m_name.length() == 0) {
        if (m_message_count == 0) {
          Log::warn("[http] empty response, downstream peer: %s", ctx->remote_addr.c_str());
        }
      } else {
        Log::warn("[http] incomplete response message, downstream peer: %s", ctx->remote_addr.c_str());
      }
      out(std::move(obj));

    // Pass all the other objects.
    } else {
      out(std::move(obj));
    }
  }

  //
  // RequestEncoder
  //

  std::string RequestEncoder::s_default_protocol("HTTP/1.1");
  std::string RequestEncoder::s_default_method("GET");
  std::string RequestEncoder::s_default_path("/");
  std::string RequestEncoder::s_header_content_length("Content-Length: ");

  RequestEncoder::RequestEncoder() {
  }

  RequestEncoder::~RequestEncoder() {
  }

  auto RequestEncoder::help() -> std::list<std::string> {
    return {
      "Frames a message body into an HTTP request message",
      "prefix = Context prefix for message info",
      "protocol = HTTP protocol that overwrites the context",
      "method = HTTP method that overwrites the context",
      "path = HTTP path that overwrites the context",
      "headers.<name> = HTTP headers to add on top of the context",
    };
  }

  void RequestEncoder::config(const std::map<std::string, std::string> &params) {
    auto prefix = utils::get_param(params, "prefix", "");
    if (!prefix.empty()) {
      m_var_protocol = prefix + ".protocol";
      m_var_method = prefix + ".method";
      m_var_path = prefix + ".path";
      m_var_headers = prefix + ".request.";
    }

    m_protocol = utils::get_param(params, "protocol", "");
    m_method = utils::get_param(params, "method", "");
    m_path = utils::get_param(params, "path", "");

    for (const auto &p : params) {
      if (!std::strncmp(p.first.c_str(), "headers.", 8)) {
        m_headers[p.first.c_str() + 8] = p.second;
      }
    }
  }

  auto RequestEncoder::clone() -> Module* {
    auto clone = new RequestEncoder();
    clone->m_var_protocol = m_var_protocol;
    clone->m_var_method = m_var_method;
    clone->m_var_path = m_var_path;
    clone->m_var_headers = m_var_headers;
    clone->m_protocol = m_protocol;
    clone->m_method = m_method;
    clone->m_path = m_path;
    clone->m_headers = m_headers;
    return clone;
  }

  void RequestEncoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {
    if (obj->is<SessionStart>() || obj->is<SessionEnd>()) {
      out(std::move(obj));

    } else if (obj->is<MessageStart>()) {
      m_buffer = make_object<Data>();

    } else if (obj->is<MessageEnd>()) {
      if (!m_buffer) return;

      std::string method;
      if (!m_method.empty()) method = ctx->evaluate(m_method);
      else if (m_var_method.empty() || !ctx->find(m_var_method, method)) method = s_default_method;

      std::string path;
      if (!m_path.empty()) path = ctx->evaluate(m_path);
      else if (m_var_path.empty() || !ctx->find(m_var_path, path)) path = s_default_path;

      std::string protocol;
      if (!m_protocol.empty()) protocol = ctx->evaluate(m_protocol);
      else if (m_var_protocol.empty() || !ctx->find(m_var_protocol, protocol)) protocol = s_default_protocol;

      auto header_data = make_object<Data>();
      header_data->push(method);
      header_data->push(' ');
      header_data->push(path);
      header_data->push(' ');
      header_data->push(protocol);
      header_data->push("\r\n");

      if (!m_var_headers.empty()) {
        write_headers_from_context(*header_data, *ctx, m_var_headers);
      }

      for (const auto &p : m_headers) {
        header_data->push(p.first);
        header_data->push(": ");
        header_data->push(ctx->evaluate(p.second));
        header_data->push("\r\n");
      }

      header_data->push(s_header_content_length);
      header_data->push(std::to_string(m_buffer->size()));
      header_data->push("\r\n\r\n");

      out(make_object<MessageStart>());
      out(std::move(header_data));
      out(std::move(m_buffer));
      out(make_object<MessageEnd>());

    } else if (auto data = obj->as<Data>()) {
      if (m_buffer) m_buffer->push(*data);
    }
  }

  //
  // ResponseEncoder
  //

  std::string ResponseEncoder::s_default_protocol("HTTP/1.1");
  std::string ResponseEncoder::s_default_status("OK");
  std::string ResponseEncoder::s_default_status_code("200");
  std::string ResponseEncoder::s_header_content_length("Content-Length: ");

  ResponseEncoder::ResponseEncoder() {
  }

  ResponseEncoder::~ResponseEncoder() {
  }

  auto ResponseEncoder::help() -> std::list<std::string> {
    return {
      "Frames a message body into an HTTP response message",
      "prefix = Context prefix for message info",
      "protocol = HTTP protocol that overwrites the context",
      "status_code = HTTP status code that overwrites the context",
      "status = HTTP status text that overwrites the context",
    };
  }

  void ResponseEncoder::config(const std::map<std::string, std::string> &params) {
    auto prefix = utils::get_param(params, "prefix", "");
    if (!prefix.empty()) {
      m_var_method = prefix + ".method";
      m_var_protocol = prefix + ".protocol";
      m_var_status_code = prefix + ".status_code";
      m_var_status = prefix + ".status";
      m_var_headers = prefix + ".response.";
      m_var_connection = prefix + ".request.connection";
      m_var_keep_alive = prefix + ".request.keep-alive";
    }

    m_protocol = utils::get_param(params, "protocol", "");
    m_status_code = utils::get_param(params, "status_code", "");
    m_status = utils::get_param(params, "status", "");

    for (const auto &p : params) {
      if (!std::strncmp(p.first.c_str(), "headers.", 8)) {
        m_headers[p.first.c_str() + 8] = p.second;
      }
    }
  }

  auto ResponseEncoder::clone() -> Module* {
    auto clone = new ResponseEncoder();
    clone->m_var_method = m_var_method;
    clone->m_var_protocol = m_var_protocol;
    clone->m_var_status_code = m_var_status_code;
    clone->m_var_status = m_var_status;
    clone->m_var_headers = m_var_headers;
    clone->m_var_connection = m_var_connection;
    clone->m_var_keep_alive = m_var_keep_alive;
    clone->m_protocol = m_protocol;
    clone->m_status_code = m_status_code;
    clone->m_status = m_status;
    clone->m_headers = m_headers;
    return clone;
  }

  void ResponseEncoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {
    if (obj->is<SessionStart>()) {
      out(std::move(obj));

    } else if (obj->is<MessageStart>()) {
      m_buffer = make_object<Data>();

    } else if (obj->is<MessageEnd>()) {
      if (!m_buffer) return;

      std::string method;
      ctx->find(m_var_method, method);

      std::string protocol;
      if (!m_protocol.empty()) protocol = ctx->evaluate(m_protocol);
      else if (m_var_protocol.empty() || !ctx->find(m_var_protocol, protocol)) protocol = s_default_protocol;

      std::string status_code;
      if (!m_status_code.empty()) status_code = ctx->evaluate(m_status_code);
      else if (m_var_status_code.empty() || !ctx->find(m_var_status_code, status_code)) status_code = s_default_status_code;

      std::string status;
      if (!m_status.empty()) m_status = ctx->evaluate(m_status);
      else if (m_var_status.empty() || !ctx->find(m_var_status, status)) status = s_default_status;

      auto header_data = make_object<Data>();
      header_data->push(protocol);
      header_data->push(' ');
      header_data->push(status_code);
      header_data->push(' ');
      header_data->push(status);
      header_data->push("\r\n");

      if (!m_var_headers.empty()) {
        write_headers_from_context(*header_data, *ctx, m_var_headers);
      }

      for (const auto &p : m_headers) {
        header_data->push(p.first);
        header_data->push(": ");
        header_data->push(ctx->evaluate(p.second));
        header_data->push("\r\n");
      }

      header_data->push(s_header_content_length);
      header_data->push(std::to_string(m_buffer->size()));
      header_data->push("\r\n\r\n");

      out(make_object<MessageStart>());
      out(std::move(header_data));
      if (method != "HEAD") out(std::move(m_buffer));
      out(make_object<MessageEnd>());

      std::string connection;
      std::string keep_alive;
      ctx->find(m_var_connection, connection);
      ctx->find(m_var_keep_alive, keep_alive);

      if (!connection.empty()) {
        if (!strcasecmp(connection.c_str(), "close")) {
          out(make_object<SessionEnd>());
        }
      } else if (keep_alive.empty()) {
        if (!strcasecmp(protocol.c_str(), "HTTP/1.0")) {
          out(make_object<SessionEnd>());
        }
      }

    } else if (auto end = obj->as<SessionEnd>()) {
      int status_code = 0;
      const char *status_text = nullptr;
      switch (end->error) {
        case SessionEnd::NO_ERROR: break;
        case SessionEnd::UNKNOWN_ERROR:
          status_code = 502;
          status_text = "Unknown Error";
          break;
        case SessionEnd::CANNOT_RESOLVE:
          status_code = 502;
          status_text = "Cannot Resolve";
          break;
        case SessionEnd::CONNECTION_REFUSED:
          status_code = 502;
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
      }
      if (status_text) {
        char header[100];
        std::string protocol;
        if (!ctx->find(m_var_protocol, protocol)) protocol = s_default_protocol;
        std::sprintf(header, "%s %d %s\r\n\r\n",
          protocol.c_str(),
          status_code,
          status_text
        );
        out(make_object<MessageStart>());
        out(make_object<Data>(header));
        out(make_object<MessageEnd>());
      }
      out(std::move(obj));

    } else if (auto data = obj->as<Data>()) {
      if (m_buffer) m_buffer->push(*data);
    }
  }

} // namespace http

NS_END
