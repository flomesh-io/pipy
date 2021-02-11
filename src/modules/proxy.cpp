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

#include "proxy.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "outbound.hpp"
#include "context.hpp"
#include "utils.hpp"
#include "logging.hpp"

NS_BEGIN

//
// Proxy
//

auto Proxy::help() -> std::list<std::string> {
  return {
    "Sends stream to a different pipline and outputs the output from that pipeline",
    "to = Name of the pipeline to send to",
  };
}

void Proxy::config(const std::map<std::string, std::string> &params) {
  m_to = utils::get_param(params, "to");
}

auto Proxy::clone() -> Module* {
  auto clone = new Proxy();
  clone->m_to = m_to;
  return clone;
}

void Proxy::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>() || obj->is<SessionEnd>()) {
    m_buffer.clear();
    m_address_known = false;
    if (m_target) {
      m_target->input(make_object<SessionEnd>());
      m_target->free();
      m_target = nullptr;
    }
    if (obj->is<SessionEnd>()) return;
  }

  if (!m_address_known) {
    bool solved = false;
    auto to = ctx->evaluate(m_to, &solved);
    if (solved && !to.empty()) {
      m_address_known = true;
      if (auto pipeline = Pipeline::get(to)) {
        auto context_id = m_context_id = ctx->id;
        m_target = pipeline->alloc(ctx);
        m_target->output([=](std::unique_ptr<Object> obj) {
          if (m_context_id != context_id) return;
          out(std::move(obj));
        });
        while (!m_buffer.empty()) {
          m_target->input(std::move(m_buffer.front()));
          m_buffer.pop_front();
        }
      } else {
        Log::error("[proxy] unknown pipeline: %s", to.c_str());
      }
    }
  }

  if (m_target) {
    m_target->input(std::move(obj));
  } else {
    m_buffer.push_back(std::move(obj));
  }
}

//
// ProxyTCP
//

auto ProxyTCP::help() -> std::list<std::string> {
  return {
    "Sends byte stream to a remote endpoint and outputs received data",
    "to = IP address and port of the remote endpoint",
    "ssl = If specified, the version of TLS connection to establish",
    "retry_count = How many times we retry connecting at most (sets to -1 for infinite retries)",
    "retry_delay = How much time we wait between retries of connecting (defaults to 5s)",
    "buffer_limit = The maximum data size allowed to buffer up (defaults to 1m)",
  };
}

void ProxyTCP::config(const std::map<std::string, std::string> &params) {
  m_to = utils::get_param(params, "to");
  m_retry_count = std::atoi(utils::get_param(params, "retry_count", "0").c_str());
  m_retry_delay = utils::get_seconds(utils::get_param(params, "retry_delay", "5s"));
  m_buffer_limit = utils::get_byte_size(utils::get_param(params, "buffer_limit", "1m"));
  m_ssl = true;
  auto ssl = utils::get_param(params, "ssl", "");
  if (ssl.empty()) m_ssl = false;
  else if (ssl == "sslv3") m_ssl_method = asio::ssl::context::sslv3_client;
  else if (ssl == "tlsv1") m_ssl_method = asio::ssl::context::tlsv1_client;
  else if (ssl == "tlsv11") m_ssl_method = asio::ssl::context::tlsv11_client;
  else if (ssl == "tlsv12") m_ssl_method = asio::ssl::context::tlsv12_client;
  else throw std::runtime_error("invalid ssl parameter");
}

auto ProxyTCP::clone() -> Module* {
  auto clone = new ProxyTCP();
  clone->m_to = m_to;
  clone->m_retry_count = m_retry_count;
  clone->m_retry_delay = m_retry_delay;
  clone->m_buffer_limit = m_buffer_limit;
  clone->m_ssl = m_ssl;
  clone->m_ssl_method = m_ssl_method;
  return clone;
}

void ProxyTCP::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>()) {
    if (m_target) {
      m_target->end();
      m_target = nullptr;
    }

    if (m_ssl) {
      asio::ssl::context ssl_context(m_ssl_method);
      m_target = new Outbound(std::move(ssl_context));
    } else {
      m_target = new Outbound();
    }

    m_target->set_retry_count(m_retry_count);
    m_target->set_retry_delay(m_retry_delay);
    m_target->set_buffer_limit(m_buffer_limit);

    m_open = false;
    try_connect(ctx, out);

  } else if (obj->is<SessionEnd>()) {
    if (m_target) {
      m_target->end();
      m_target = nullptr;
    }
    out(std::move(obj));

  } else if (auto data = obj->as<Data>()) {
    if (m_target) {
      try_connect(ctx, out);
      obj.release();
      m_target->send(std::unique_ptr<Data>(data));
    }

  } else if (obj->is<MessageEnd>()) {
    if (m_target) {
      m_target->flush();
    }
  }
}

void ProxyTCP::try_connect(
  std::shared_ptr<Context> ctx,
  Object::Receiver out
) {
  if (!m_open) {
    auto to = ctx->evaluate(m_to);
    if (!to.empty()) {
      m_open = true;
      std::string host; int port;
      if (utils::get_host_port(to, host, port)) {
        m_target->connect(host, port, out);
      } else {
        Log::error("[proxy-tcp] invalid target: %s", to.c_str());
      }
    }
  }
}

NS_END
