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

#include "inbound.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "context.hpp"
#include "constants.hpp"
#include "logging.hpp"

NS_BEGIN

using tcp = asio::ip::tcp;

Inbound::Inbound()
  : m_socket(g_io_service)
{
}

Inbound::~Inbound() {
  if (m_session) {
    m_session->free();
  }
}

void Inbound::accept(
  Pipeline *pipeline,
  asio::ip::tcp::acceptor &acceptor,
  std::function<void(const std::error_code&)> on_result
) {
  acceptor.async_accept(m_socket, m_peer, [=](const std::error_code &ec) {
    m_peer_addr = m_peer.address().to_string();
    if (ec) {
      if (ec != asio::error::operation_aborted) {
        Log::warn(
          "Inbound: %p, error accepting from downstream %s, %s",
          this, m_peer_addr.c_str(), ec.message().c_str());
      }
      delete this;
    } else {
      auto session = pipeline->alloc();
      session->output([=](std::unique_ptr<Object> obj) {
        if (auto data = obj->as<Data>()) {
          obj.release();
          send(std::unique_ptr<Data>(data));
        } else if (obj->is<MessageEnd>()) {
          flush();
        } else if (obj->is<SessionEnd>()) {
          end();
        }
      });
      auto &ctx = session->context();
      ctx.remote_addr = m_peer_addr;
      ctx.remote_port = m_peer.port();
      ctx.local_addr = m_socket.local_endpoint().address().to_string();;
      ctx.local_port = m_socket.local_endpoint().port();
      m_session = session;
      Log::debug(
        "Inbound: %p, connection accepted from downstream %s",
        this, m_peer_addr.c_str());
      session->input(make_object<SessionStart>());
      receive();
    }
    on_result(ec);
  });
}

void Inbound::send(std::unique_ptr<Data> data) {
  if (!m_writing_ended) {
    if (data->size() > 0) {
      m_buffer.push(*data);
      if (m_buffer.size() >= SEND_BUFFER_FLUSH_SIZE) pump();
    } else {
      pump();
    }
  }
}

void Inbound::flush() {
  if (!m_writing_ended) {
    pump();
  }
}

void Inbound::end() {
  if (!m_writing_ended) {
    m_writing_ended = true;
    pump();
    close();
    free();
  }
}

void Inbound::receive() {
  auto buffer = new Data(RECEIVE_BUFFER_SIZE);

  auto on_received = [=](const std::error_code &ec, std::size_t n) {
    if (n > 0) {
      buffer->pop(buffer->size() - n);
      m_session->input(std::unique_ptr<Object>(buffer));
      m_session->input(make_object<Data>()); // flush
    } else {
      delete buffer;
    }

    if (ec) {
      if (ec == asio::error::eof || ec == asio::error::operation_aborted) {
        Log::debug(
          "Inbound: %p, connection closed by downstream %s",
          this, m_peer_addr.c_str());
        m_session->input(make_object<SessionEnd>(SessionEnd::NO_ERROR));
      } else {
        auto msg = ec.message();
        Log::warn(
          "Inbound: %p, error reading from downstream %s, %s",
          this, m_peer_addr.c_str(), msg.c_str());
        m_session->input(make_object<SessionEnd>(SessionEnd::READ_ERROR));
      }

      m_reading_ended = true;
      free();

    } else {
      receive();
    }
  };

  m_socket.async_read_some(
    DataChunks(buffer->chunks()),
    on_received);
}

void Inbound::pump() {
  if (m_pumping) return;
  if (m_buffer.empty()) return;

  m_socket.async_write_some(
    DataChunks(m_buffer.chunks()),
    [=](const std::error_code &ec, std::size_t n) {
      m_buffer.shift(n);
      m_pumping = false;

      if (ec) {
        auto msg = ec.message();
        Log::warn(
          "Inbound: %p, error writing to downstream %s, %s",
          this, m_peer_addr.c_str(), msg.c_str());
        m_buffer.clear();
        m_writing_ended = true;

      } else if (m_buffer.size() > 0) {
        pump();
      }

      if (m_writing_ended) {
        close();
        free();
      }
    }
  );

  m_pumping = true;
}

void Inbound::close() {
  if (m_pumping) return;

  std::error_code ec;
  m_socket.close(ec);
  if (ec) {
    Log::error("Inbound: %p, error closing socket to %s, %s", this, m_peer_addr.c_str(), ec.message().c_str());
  } else {
    Log::debug("Inbound: %p, connection closed to %s", this, m_peer_addr.c_str());
  }
}

void Inbound::free() {
  if (m_reading_ended && m_writing_ended && m_buffer.empty()) {
    delete this;
  }
}

NS_END