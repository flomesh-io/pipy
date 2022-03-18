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
#include "module.hpp"
#include "worker.hpp"
#include "constants.hpp"
#include "logging.hpp"

namespace pipy {

using tcp = asio::ip::tcp;

uint64_t Inbound::s_inbound_id = 0;

Inbound::Inbound(Listener *listener, const Options &options)
  : m_listener(listener)
  , m_socket(Net::context())
  , m_options(options)
{
  Log::debug("[inbound  %p] ++", this);
  if (!++s_inbound_id) s_inbound_id++;
  m_id = s_inbound_id;
}

Inbound::~Inbound() {
  Log::debug("[inbound  %p] --", this);
  if (m_pipeline) {
    m_listener->close(this);
  }
}

auto Inbound::remote_address() -> pjs::Str* {
  if (!m_str_remote_addr) {
    m_str_remote_addr = pjs::Str::make(m_remote_addr);
  }
  return m_str_remote_addr;
}

auto Inbound::local_address() -> pjs::Str* {
  if (!m_str_local_addr) {
    m_str_local_addr = pjs::Str::make(m_local_addr);
  }
  return m_str_local_addr;
}

void Inbound::accept(asio::ip::tcp::acceptor &acceptor) {
  acceptor.async_accept(
    m_socket, m_peer,
    [this](const std::error_code &ec) {
      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc);
            Log::error("%s error accepting connection: %s", desc, ec.message().c_str());
          }

        } else {
          if (Log::is_enabled(Log::DEBUG)) {
            char desc[200];
            describe(desc);
            Log::debug("%s connection accepted", desc);
          }
          const auto &ep = m_socket.local_endpoint();
          m_local_addr = ep.address().to_string();
          m_local_port = ep.port();
          m_remote_addr = m_peer.address().to_string();
          m_remote_port = m_peer.port();
          start();
        }
      }

      release();
    }
  );

  retain();
}

void Inbound::on_tap_open() {
  switch (m_receiving_state) {
    case PAUSING:
      m_receiving_state = RECEIVING;
      break;
    case PAUSED:
      m_receiving_state = RECEIVING;
      receive();
      release();
      break;
    default: break;
  }
}

void Inbound::on_tap_close() {
  if (m_receiving_state == RECEIVING) {
    m_receiving_state = PAUSING;
  }
}

void Inbound::on_event(Event *evt) {
  if (!m_ended) {
    if (auto data = evt->as<Data>()) {
      if (data->size() > 0) {
        m_buffer.push(*data);
        if (m_buffer.size() >= SEND_BUFFER_FLUSH_SIZE) pump();
      } else {
        pump();
      }

    } else if (evt->is<MessageEnd>()) {
      pump();

    } else if (evt->is<StreamEnd>()) {
      m_ended = true;
      if (m_buffer.empty()) {
        close(StreamEnd::NO_ERROR);
      } else {
        pump();
      }
    }
  }
}

void Inbound::start() {
  auto def = m_listener->pipeline_def();
  auto mod = def->module();
  auto ctx = mod
    ? mod->worker()->new_runtime_context()
    : new pipy::Context();
  ctx->m_inbound = this;
  auto p = Pipeline::make(def, ctx);
  p->chain(EventTarget::input());
  m_pipeline = p;
  m_output = p->input();
  m_listener->open(this);
  receive();
}

void Inbound::receive() {
  static Data::Producer s_data_producer("Inbound");
  pjs::Ref<Data> buffer(Data::make(RECEIVE_BUFFER_SIZE, &s_data_producer));

  m_socket.async_read_some(
    DataChunks(buffer->chunks()),
    [=](const std::error_code &ec, std::size_t n) {
      if (m_options.read_timeout > 0){
        m_read_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (n > 0) {
          InputContext ic(this);
          buffer->pop(buffer->size() - n);
          output(buffer);
          output(Data::flush());
        }

        if (ec) {
          if (ec == asio::error::eof) {
            if (Log::is_enabled(Log::DEBUG)) {
              char desc[200];
              describe(desc);
              Log::debug("%s closed by peer", desc);
            }
            close(StreamEnd::NO_ERROR);
          } else {
            if (Log::is_enabled(Log::WARN)) {
              char desc[200];
              describe(desc);
              Log::warn("%s error reading from peer: %s", desc, ec.message().c_str());
            }
            close(StreamEnd::READ_ERROR);
          }

        } else if (m_receiving_state == PAUSING) {
          m_receiving_state = PAUSED;
          retain();

        } else if (m_receiving_state == RECEIVING) {
          receive();
        }
      }

      release();
    }
  );

  if (m_options.read_timeout > 0) {
    m_read_timer.schedule(
      m_options.read_timeout,
      [this]() {
        close(StreamEnd::READ_TIMEOUT);
      }
    );
  }

  retain();
}

void Inbound::pump() {
  if (m_pumping) return;
  if (m_buffer.empty()) return;

  m_socket.async_write_some(
    DataChunks(m_buffer.chunks()),
    [=](const std::error_code &ec, std::size_t n) {
      m_pumping = false;

      if (m_options.write_timeout > 0) {
        m_write_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        m_buffer.shift(n);

        if (ec) {
          if (Log::is_enabled(Log::WARN)) {
            char desc[200];
            describe(desc);
            Log::warn("%s error writing to peer: %s", desc, ec.message().c_str());
          }
          close(StreamEnd::WRITE_ERROR);

        } else if (m_ended && m_buffer.empty()) {
          close(StreamEnd::NO_ERROR);

        } else {
          pump();
        }
      }

      release();
    }
  );

  if (m_options.write_timeout > 0) {
    m_write_timer.schedule(
      m_options.write_timeout,
      [this]() {
        close(StreamEnd::WRITE_TIMEOUT);
      }
    );
  }

  m_pumping = true;

  retain();
}

void Inbound::output(Event *evt) {
  m_output->input(evt);
}

void Inbound::close(StreamEnd::Error err) {
  std::error_code ec;
  m_socket.shutdown(tcp::socket::shutdown_both, ec);
  m_socket.close(ec);

  if (ec) {
    if (Log::is_enabled(Log::ERROR)) {
      char desc[200];
      describe(desc);
      Log::error("%s error closing socket: %s", desc, ec.message().c_str());
    }
  } else {
    if (Log::is_enabled(Log::DEBUG)) {
      char desc[200];
      describe(desc);
      Log::debug("%s connection closed to peer", desc);
    }
  }

  InputContext ic(this);
  output(StreamEnd::make(err));
}

void Inbound::describe(char *buf) {
  std::sprintf(
    buf, "[inbound  %p] [%s]:%d -> [%s]:%d",
    this,
    m_remote_addr.c_str(),
    m_remote_port,
    m_local_addr.c_str(),
    m_local_port
  );
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
