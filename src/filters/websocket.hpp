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

#ifndef WEBSOCKET_HPP
#define WEBSOCKET_HPP

#include "filter.hpp"
#include "deframer.hpp"

#include <random>

namespace pipy {
namespace websocket {

//
// MessageHead
//

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  enum class Field {
    opcode,
    masked,
  };

  auto opcode() -> int {
    pjs::Value ret;
    pjs::get<MessageHead>(this, Field::opcode, ret);
    return ret.to_number();
  }

  auto masked() -> bool {
    pjs::Value ret;
    pjs::get<MessageHead>(this, Field::masked, ret);
    return ret.to_boolean();
  }

  void opcode(int v) { pjs::set<MessageHead>(this, Field::opcode, v); }
  void masked(bool v) { pjs::set<MessageHead>(this, Field::masked, v); }
};

//
// Decoder
//

class Decoder : public Filter, public Deframer {
public:
  Decoder();

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

private:
  enum State {
    OPCODE,
    LENGTH,
    LENGTH_16,
    LENGTH_64,
    MASK,
    PAYLOAD,
  };

  uint8_t m_opcode;
  uint8_t m_buffer[8];
  uint64_t m_payload_size;
  uint8_t m_mask[4];
  uint8_t m_mask_pointer;
  bool m_has_mask;
  bool m_started;

  virtual auto on_state(int state, int c) -> int override;
  virtual void on_pass(const Data &data) override;

  auto message_start() -> State;
  void message_end();
};

//
// Encoder
//

class Encoder : public Filter {
public:
  Encoder();

private:
  Encoder(const Encoder &r);
  ~Encoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void shutdown() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

private:
  Data m_buffer;
  pjs::Ref<MessageStart> m_start;
  pjs::PropertyCache m_prop_opcode;
  pjs::PropertyCache m_prop_masked;
  std::minstd_rand m_rand;
  uint8_t m_opcode;
  bool m_masked;
  bool m_continuation;
  bool m_shutdown = false;

  void frame(const Data &data, bool final);
};

} // namespace websocket
} // namespace pipy

#endif // WEBSOCKET_HPP
