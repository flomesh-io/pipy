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
#include "listener.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "module.hpp"
#include "worker.hpp"
#include "constants.hpp"
#include "logging.hpp"

namespace pipy {

using tcp = asio::ip::tcp;

uint64_t Inbound::s_inbound_id = 0;

Inbound::Inbound(Listener *listener)
  : m_listener(listener)
  , m_socket(Net::service())
{
  if (!++s_inbound_id) s_inbound_id++;
  m_id = s_inbound_id;
}

Inbound::~Inbound() {
  if (m_session) {
    auto *ctx = m_session->context();
    ctx->m_inbound = nullptr;
  }
  m_listener->close(this);
}

void Inbound::accept(
  Listener *listener,
  asio::ip::tcp::acceptor &acceptor,
  std::function<void(const std::error_code&)> on_result
) {
  retain();
  acceptor.async_accept(
    m_socket, m_peer,
    [=](const std::error_code &ec) {
      m_remote_addr = m_peer.address().to_string();
      m_remote_port = m_peer.port();
      on_result(ec);
      if (ec) {
        if (ec != asio::error::operation_aborted) {
          Log::warn(
            "Inbound: %p, error accepting from downstream %s, %s",
            this, m_remote_addr.c_str(), ec.message().c_str());
        }
        release();
      } else {
        const auto &local = m_socket.local_endpoint();
        m_local_addr = local.address().to_string();
        m_local_port = local.port();

        Log::debug(
          "Inbound: %p, connection accepted from downstream %s",
          this, m_remote_addr.c_str());

        start(listener->pipeline());
      }
    }
  );
}

bool Inbound::increase_response_count() {
  m_response_count++;
  if (m_response_count == m_request_count) {
    return m_keep_alive;
  } else {
    return true;
  }
}

void Inbound::pause() {
  if (m_receiving_state == RECEIVING) {
    m_receiving_state = PAUSING;
  }
}

void Inbound::resume() {
  switch (m_receiving_state) {
    case PAUSING:
      m_receiving_state = RECEIVING;
      break;
    case PAUSED:
      m_receiving_state = RECEIVING;
      receive();
      break;
    default: break;
  }
}

void Inbound::send(const pjs::Ref<Data> &data) {
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

void Inbound::start(Pipeline *pipeline) {
  auto mod = pipeline->module();
  auto ctx = mod
    ? mod->worker()->new_runtime_context()
    : new pipy::Context(nullptr, nullptr, nullptr); // for the internal GUI service only
  ctx->m_inbound = this;
  m_session = Session::make(ctx, pipeline);
  m_session->on_output([=](const pjs::Ref<Event> &obj) {
    if (auto data = obj->as<Data>()) {
      send(data);
    } else if (obj->is<MessageEnd>()) {
      flush();
    } else if (obj->is<SessionEnd>()) {
      end();
    }
  });
  receive();
}

void Inbound::receive() {
  static Data::Producer s_data_producer("Inbound");

  pjs::Ref<Data> buffer(Data::make(RECEIVE_BUFFER_SIZE, &s_data_producer));

  auto on_received = [=](const std::error_code &ec, std::size_t n) {
    if (n > 0) {
      buffer->pop(buffer->size() - n);
      m_session->input(buffer);
      m_session->input(Data::flush());
    }

    if (ec) {
      if (ec == asio::error::eof || ec == asio::error::operation_aborted) {
        Log::debug(
          "Inbound: %p, connection closed by downstream %s",
          this, m_remote_addr.c_str());
        m_session->input(SessionEnd::make(SessionEnd::NO_ERROR));
      } else {
        auto msg = ec.message();
        Log::warn(
          "Inbound: %p, error reading from downstream %s, %s",
          this, m_remote_addr.c_str(), msg.c_str());
        m_session->input(SessionEnd::make(SessionEnd::READ_ERROR));
      }

      m_reading_ended = true;
      m_writing_ended = true;
      free();

    } else {
      if (m_receiving_state == PAUSING) m_receiving_state = PAUSED;
      if (m_receiving_state == RECEIVING) receive();
    }
  };

  m_socket.async_read_some(
    DataChunks(buffer->chunks()),
    on_received);
}

void Inbound::pump() {
  if (m_pumping) return;
  if (m_buffer.empty()) return;

  auto on_sent = [=](const std::error_code &ec, std::size_t n) {
    m_buffer.shift(n);
    m_pumping = false;

    if (ec) {
      auto msg = ec.message();
      Log::warn(
        "Inbound: %p, error writing to downstream %s, %s",
        this, m_remote_addr.c_str(), msg.c_str());
      m_buffer.clear();
      m_writing_ended = true;

    } else {
      pump();
    }

    if (m_writing_ended) {
      close();
      free();
    }
  };

  m_socket.async_write_some(
    DataChunks(m_buffer.chunks()),
    on_sent);

  m_pumping = true;
}

void Inbound::close() {
  if (m_pumping) return;

  std::error_code ec;
  m_socket.close(ec);
  if (ec) {
    Log::error("Inbound: %p, error closing socket to %s, %s", this, m_remote_addr.c_str(), ec.message().c_str());
  } else {
    Log::debug("Inbound: %p, connection closed to %s", this, m_remote_addr.c_str());
  }
}

void Inbound::free() {
  if (!m_pumping && m_reading_ended && m_writing_ended) {
    m_session = nullptr;
    release();
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Inbound>::init() {
  accessor("id"           , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->id()); });
  accessor("remoteAddress", [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->remote_address()); });
  accessor("remotePort"   , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->remote_port()); });
  accessor("localAddress" , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->local_address()); });
  accessor("localPort"    , [](Object *obj, Value &ret) { ret.set(obj->as<Inbound>()->local_port()); });
}

} // namespace pjs