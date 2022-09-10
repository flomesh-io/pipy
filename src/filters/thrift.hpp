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

#ifndef THRIFT_HPP
#define THRIFT_HPP

#include "filter.hpp"
#include "deframer.hpp"
#include "options.hpp"

namespace pipy {
namespace thrift {

//
// MessageHead
//

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  enum class Field {
    seqID,
    type,
    name,
    protocol,
  };

  auto seqID() -> int {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::name, ret);
    return ret.is_number() ? ret.n() : 0;
  }

  auto type() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::type, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  auto name() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::name, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  auto protocol() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::protocol, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  void seqID(int n) { pjs::set<MessageHead>(this, MessageHead::Field::seqID, n); }
  void type(pjs::Str *s) { pjs::set<MessageHead>(this, MessageHead::Field::type, s); }
  void name(pjs::Str *s) { pjs::set<MessageHead>(this, MessageHead::Field::name, s); }
  void protocol(pjs::Str *s) { pjs::set<MessageHead>(this, MessageHead::Field::protocol, s); }
};

//
// Decoder
//

class Decoder : public Filter, public Deframer {
public:
  struct Options : public pipy::Options {
    bool body = false;

    Options() {}
    Options(pjs::Object *options);
  };

  Decoder(const Options &options);

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

private:
  enum Format {
    BINARY,
    BINARY_OLD,
    COMPACT,
  };

  enum State {
    START,
    MESSAGE_HEAD,
    MESSAGE_NAME,
    MESSAGE_TYPE,
    SEQ_ID,
    STRUCT_FIELD_TYPE,
    STRUCT_FIELD_ID,
    VALUE_BOOL,
    VALUE_I8,
    VALUE_I16,
    VALUE_I32,
    VALUE_I64,
    VALUE_DOUBLE,
    VALUE_UUID,
    BINARY_SIZE,
    BINARY_DATA,
    LIST_HEAD,
    SET_HEAD,
    MAP_HEAD,
    ERROR = -1,
  };

  struct Level : public pjs::Pooled<Level> {
    enum Kind {
      STRUCT,
      LIST,
      SET,
      MAP,
    };

    Level* back;
    Kind kind;
    State element_types[2];
    int element_sizes[2];
    int size;
    int index;
    pjs::Value key;
    pjs::Ref<pjs::Object> obj;
  };

  Options m_options;
  Format m_format;
  uint8_t m_read_buf[16];
  pjs::Ref<Data> m_read_data;
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Object> m_payload;
  Level* m_stack = nullptr;
  bool m_started = false;

  virtual auto on_state(int state, int c) -> int override;
  virtual void on_pass(const Data &data) override;

  bool set_message_type(int type);
  void set_value_type(int type, State &state, int &read_size);
  auto set_value_start() -> State;
  auto set_value_end() -> State;
  void set_value(const pjs::Value &v);
  auto push_struct(pjs::Object *obj) -> State;
  auto push_list(int type, int size, pjs::Object *obj) -> State;
  auto push_set(int type, int size, pjs::Object *obj) -> State;
  auto push_map(int key_type, int value_type, int size, pjs::Object *obj) -> State;
  auto pop() -> State;
  void message_start();
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
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

private:
  bool m_started = false;
  pjs::PropertyCache m_prop_seqID;
  pjs::PropertyCache m_prop_type;
  pjs::PropertyCache m_prop_name;
  pjs::PropertyCache m_prop_protocol;
};

} // namespace thrift
} // namespace pipy

#endif // THRIFT_HPP
