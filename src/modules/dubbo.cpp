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

#include "dubbo.hpp"
#include "logging.hpp"
#include "utils.hpp"

NS_BEGIN

namespace dubbo {

  //
  // Decoder
  //

  Decoder::Decoder() {
  }

  Decoder::~Decoder() {
  }

  auto Decoder::help() -> std::list<std::string> {
    return {
      "Deframes a Dubbo message",
      "prefix = Prefix of the context variables where decoded message header will be stored",
    };
  }

  void Decoder::config(const std::map<std::string, std::string> &params) {
    auto prefix = utils::get_param(params, "prefix", "");
    if (!prefix.empty()) {
      m_var_request_id = prefix + "request_id";
      m_var_request_bit = prefix + "request_bit";
      m_var_2_way_bit = prefix + "2_way_bit";
      m_var_event_bit = prefix + "event_bit";
      m_var_status = prefix + "status";
    }
  }

  auto Decoder::clone() -> Module* {
    auto clone = new Decoder();
    clone->m_var_request_id = m_var_request_id;
    clone->m_var_request_bit = m_var_request_bit;
    clone->m_var_2_way_bit = m_var_2_way_bit;
    clone->m_var_event_bit = m_var_event_bit;
    clone->m_var_status = m_var_status;
    return clone;
  }

  void Decoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Session start.
    if (obj->is<SessionStart>()) {
      m_state = FRAME_HEAD;
      m_size = 0;
      m_head.clear();
      out(std::move(obj));

    // Data.
    } else if (auto data = obj->as<Data>()) {
      while (!data->empty()) {
        auto old_state = m_state;
        auto read = data->shift_until([&](int c) {
          if (m_state != old_state)
            return true;

          // Parse one character.
          switch (m_state) {

          // Read frame header.
          case FRAME_HEAD:
            m_head.push(c);
            if (m_head.length() == 16) {
              if ((unsigned char)m_head[0] != 0xda ||
                  (unsigned char)m_head[1] != 0xbb
              ) {
                Log::error("[dubbo] magic number not found");
              }

              auto F = m_head[2];
              auto S = m_head[3];
              auto R = ((long long)(unsigned char)m_head[4] << 56)
                    | ((long long)(unsigned char)m_head[5] << 48)
                    | ((long long)(unsigned char)m_head[6] << 40)
                    | ((long long)(unsigned char)m_head[7] << 32)
                    | ((long long)(unsigned char)m_head[8] << 24)
                    | ((long long)(unsigned char)m_head[9] << 16)
                    | ((long long)(unsigned char)m_head[10] << 8)
                    | ((long long)(unsigned char)m_head[11] << 0);
              auto L = ((int)(unsigned char)m_head[12] << 24)
                    | ((int)(unsigned char)m_head[13] << 16)
                    | ((int)(unsigned char)m_head[14] << 8)
                    | ((int)(unsigned char)m_head[15] << 0);

              if (!m_var_request_id.empty()) ctx->variables[m_var_request_id] = std::to_string(R);
              if (!m_var_request_bit.empty()) ctx->variables[m_var_request_bit] = F & 0x80 ? "1" : "0";
              if (!m_var_2_way_bit.empty()) ctx->variables[m_var_2_way_bit] = F & 0x40 ? "1" : "0";
              if (!m_var_event_bit.empty()) ctx->variables[m_var_event_bit] = F & 0x20 ? "1" : "0";
              if (!m_var_status.empty()) ctx->variables[m_var_status] = std::to_string((unsigned char)S);

              m_size = L;
              m_state = FRAME_DATA;
            }
            break;

          // Read data.
          case FRAME_DATA:
            if (!--m_size) {
              m_state = FRAME_HEAD;
              m_head.clear();
            }
            break;
          }
          return false;
        });

        // Pass the body data.
        if (old_state == FRAME_DATA) {
          if (!read.empty()) out(make_object<Data>(std::move(read)));
          if (m_state != FRAME_DATA) out(make_object<MessageEnd>());

        // Start of the body.
        } else if (m_state == FRAME_DATA) {
          out(make_object<MessageStart>());
          if (m_size == 0) {
            out(make_object<MessageEnd>());
            m_state = FRAME_HEAD;
            m_head.clear();
          }
        }
      }

    // Pass all the other objects.
    } else {
      out(std::move(obj));
    }
  }

  //
  // Encoder
  //

  Encoder::Encoder() {
  }

  Encoder::~Encoder() {
  }

  auto Encoder::help() -> std::list<std::string> {
    return {
      "Frames a Dubbo message",
      "prefix = Prefix of the context variables where message header is provided",
    };
  }

  void Encoder::config(const std::map<std::string, std::string> &params) {
    auto prefix = utils::get_param(params, "prefix", "");
    if (!prefix.empty()) {
      m_var_request_id = prefix + "request_id";
      m_var_request_bit = prefix + "request_bit";
      m_var_2_way_bit = prefix + "2_way_bit";
      m_var_event_bit = prefix + "event_bit";
      m_var_status = prefix + "status";
    }
  }

  auto Encoder::clone() -> Module* {
    auto clone = new Encoder();
    clone->m_var_request_id = m_var_request_id;
    clone->m_var_request_bit = m_var_request_bit;
    clone->m_var_2_way_bit = m_var_2_way_bit;
    clone->m_var_event_bit = m_var_event_bit;
    clone->m_var_status = m_var_status;
    return clone;
  }

  void Encoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {
    if (obj->is<MessageStart>()) {
      m_buffer = make_object<Data>();

    } else if (obj->is<MessageEnd>()) {
      if (m_buffer) {
        auto R = get_header(*ctx, m_var_request_id, m_auto_request_id++);
        char F = get_header(*ctx, m_var_request_bit, 1) ? 0x82 : 0x02;
        auto D = get_header(*ctx, m_var_2_way_bit, 1);
        auto E = get_header(*ctx, m_var_event_bit, 0);
        auto S = get_header(*ctx, m_var_status, 0);
        auto L = m_buffer->size();

        if (D) F |= 0x40;
        if (E) F |= 0x20;

        char head[16];
        head[0] = 0xda;
        head[1] = 0xbb;
        head[2] = F;
        head[3] = S;
        head[4] = R >> 56;
        head[5] = R >> 48;
        head[6] = R >> 40;
        head[7] = R >> 32;
        head[8] = R >> 24;
        head[9] = R >> 16;
        head[10] = R >> 8;
        head[11] = R >> 0;
        head[12] = L >> 24;
        head[13] = L >> 16;
        head[14] = L >> 8;
        head[15] = L >> 0;

        out(make_object<MessageStart>());
        out(make_object<Data>(head, sizeof(head)));
        out(std::move(m_buffer));
      }
      out(make_object<MessageEnd>());
      m_buffer.reset();

    } else if (auto data = obj->as<Data>()) {
      if (m_buffer) m_buffer->push(*data);

    } else {
      if (obj->is<SessionStart>()) m_auto_request_id = 0;
      out(std::move(obj));
    }
  }

  long long Encoder::get_header(
    const Context &ctx,
    const std::string &name,
    long long value
  ) {
    if (name.empty()) return value;
    auto i = ctx.variables.find(name);
    if (i == ctx.variables.end()) return value;
    return std::atoll(i->second.c_str());
  }

} // namespace dubbo

NS_END

