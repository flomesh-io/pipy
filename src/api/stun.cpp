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
 *  SOFTWARE IS PROVIDED IN AN "AS IS" CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "stun.hpp"

#include <cstring>
#include <cstdio>

namespace pipy {

// STUN constants
static const uint16_t STUN_BINDING_REQUEST = 0x0001;
static const uint16_t STUN_BINDING_RESPONSE = 0x0101;
static const uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
static const uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

static Data::Producer s_dp("STUN");

thread_local static pjs::ConstStr STR_type("type");
thread_local static pjs::ConstStr STR_transactionId("transactionId");
thread_local static pjs::ConstStr STR_mappedAddress("mappedAddress");
thread_local static pjs::ConstStr STR_ip("ip");
thread_local static pjs::ConstStr STR_port("port");
thread_local static pjs::ConstStr STR_family("family");
thread_local static pjs::ConstStr STR_BindingRequest("BindingRequest");
thread_local static pjs::ConstStr STR_BindingResponse("BindingResponse");
thread_local static pjs::ConstStr STR_IPv4("IPv4");
thread_local static pjs::ConstStr STR_IPv6("IPv6");

//
// STUN
//

auto STUN::encode(pjs::Object *msg) -> Data* {
  if (!msg) {
    throw std::runtime_error("STUN encode: message is null");
  }

  pjs::Value type_val;
  msg->get(STR_type, type_val);
  if (!type_val.is_string()) {
    throw std::runtime_error("STUN encode: type must be a string");
  }

  auto type_str = type_val.s();
  if (type_str->str() != "BindingRequest") {
    throw std::runtime_error("STUN encode: only BindingRequest is supported");
  }

  pjs::Value tid_val;
  msg->get(STR_transactionId, tid_val);
  if (!tid_val.is_instance_of<Data>()) {
    throw std::runtime_error("STUN encode: transactionId must be Data");
  }

  auto tid_data = tid_val.as<Data>();
  auto tid_bytes = tid_data->to_bytes();
  if (tid_bytes.size() != 12) {
    throw std::runtime_error("STUN encode: transactionId must be 12 bytes");
  }

  uint8_t request[20];

  // Message Type: Binding Request
  request[0] = (STUN_BINDING_REQUEST >> 8) & 0xFF;
  request[1] = STUN_BINDING_REQUEST & 0xFF;

  // Message Length: 0 (no attributes)
  request[2] = 0;
  request[3] = 0;

  // Magic Cookie
  request[4] = (STUN_MAGIC_COOKIE >> 24) & 0xFF;
  request[5] = (STUN_MAGIC_COOKIE >> 16) & 0xFF;
  request[6] = (STUN_MAGIC_COOKIE >> 8) & 0xFF;
  request[7] = STUN_MAGIC_COOKIE & 0xFF;

  // Transaction ID
  std::memcpy(&request[8], tid_bytes.data(), 12);

  return Data::make(request, sizeof(request), &s_dp);
}

auto STUN::decode(const Data &data) -> pjs::Object* {
  auto result = pjs::Object::make();

  // Get data as bytes
  auto bytes = data.to_bytes();
  if (bytes.size() < 20) {
    throw std::runtime_error("STUN decode: message too short");
  }

  const uint8_t *buf = bytes.data();
  size_t len = bytes.size();

  // Check message type
  uint16_t msg_type = (buf[0] << 8) | buf[1];
  
  if (msg_type == STUN_BINDING_REQUEST) {
    result->set(STR_type, STR_BindingRequest.get());
  } else if (msg_type == STUN_BINDING_RESPONSE) {
    result->set(STR_type, STR_BindingResponse.get());
  } else {
    throw std::runtime_error("STUN decode: unknown message type");
  }

  // Check magic cookie
  uint32_t magic = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
  if (magic != STUN_MAGIC_COOKIE) {
    throw std::runtime_error("STUN decode: invalid magic cookie");
  }

  // Extract transaction ID
  auto tid = Data::make(&buf[8], 12, &s_dp);
  result->set(STR_transactionId, tid);

  // Parse message length
  uint16_t msg_len = (buf[2] << 8) | buf[3];
  if (len < 20 + msg_len) {
    throw std::runtime_error("STUN decode: message length mismatch");
  }

  // Parse attributes (only for responses)
  if (msg_type == STUN_BINDING_RESPONSE) {
    size_t pos = 20;
    while (pos + 4 <= len) {
      uint16_t attr_type = (buf[pos] << 8) | buf[pos + 1];
      uint16_t attr_len = (buf[pos + 2] << 8) | buf[pos + 3];
      pos += 4;

      if (pos + attr_len > len) break;

      if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS || attr_type == STUN_ATTR_MAPPED_ADDRESS) {
        if (attr_len >= 8) {
          uint8_t family = buf[pos + 1];
          uint16_t port = (buf[pos + 2] << 8) | buf[pos + 3];

          if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
            port ^= (STUN_MAGIC_COOKIE >> 16);
          }

          auto addr = pjs::Object::make();

          if (family == 0x01) { // IPv4
            uint32_t ip = (buf[pos + 4] << 24) | (buf[pos + 5] << 16) |
                          (buf[pos + 6] << 8) | buf[pos + 7];

            if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
              ip ^= STUN_MAGIC_COOKIE;
            }

            char ip_str[16];
            std::snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
              (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
              (ip >> 8) & 0xFF, ip & 0xFF);

            addr->set(STR_ip, pjs::Str::make(ip_str));
            addr->set(STR_port, port);
            addr->set(STR_family, STR_IPv4.get());
            result->set(STR_mappedAddress, addr);
            break;
          } else if (family == 0x02 && attr_len >= 20) { // IPv6
            uint8_t ip[16];
            for (int i = 0; i < 16; i++) {
              ip[i] = buf[pos + 4 + i];
              if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                if (i < 4) {
                  ip[i] ^= (STUN_MAGIC_COOKIE >> (24 - i * 8)) & 0xFF;
                } else {
                  ip[i] ^= buf[8 + i - 4]; // XOR with transaction ID
                }
              }
            }

            char ip_str[40];
            std::snprintf(ip_str, sizeof(ip_str),
              "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
              ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7],
              ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15]);

            addr->set(STR_ip, pjs::Str::make(ip_str));
            addr->set(STR_port, port);
            addr->set(STR_family, STR_IPv6.get());
            result->set(STR_mappedAddress, addr);
            break;
          }
        }
      }

      pos += attr_len;
      pos = (pos + 3) & ~3; // Align to 4-byte boundary
    }
  }

  return result;
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template <> void ClassDef<STUN>::init() {
  ctor();

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Object *msg = nullptr;
    if (!ctx.arguments(1, &msg) || !msg) {
      ctx.error("STUN.encode: argument must be an object");
      return;
    }
    try {
      ret.set(STUN::encode(msg));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data = nullptr;
    if (!ctx.arguments(1, &data) || !data) {
      ctx.error("STUN.decode: argument must be Data");
      return;
    }
    try {
      ret.set(STUN::decode(*data));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

} // namespace pjs
