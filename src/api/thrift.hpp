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

#ifndef API_THRIFT_HPP
#define API_THRIFT_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"
#include "deframer.hpp"

namespace pipy {

class Data;

//
// Thrift
//

class Thrift : public pjs::ObjectTemplate<Thrift> {
public:
  static auto decode(const Data &data) -> pjs::Array*;
  static void encode(pjs::Object *msg, Data &data);
  static void encode(pjs::Object *mag, Data::Builder &db);

  //
  // Thrift::Protocol
  //

  enum class Protocol {
    binary,
    compact,
    old,
  };

  //
  // Thrift::Type
  //

  enum class Type {
    BOOL,
    I8,
    I16,
    I32,
    I64,
    DOUBLE,
    BINARY,
    STRUCT,
    MAP,
    SET,
    LIST,
    UUID,
  };

  //
  // Thrift::Field
  //

  class Field : public pjs::ObjectTemplate<Field> {
  public:
    int id = 0;
    pjs::EnumValue<Type> type = Type::I32;
    pjs::Value value;
  };

  //
  // Thrift::List
  //

  class List : public pjs::ObjectTemplate<List> {
  public:
    pjs::EnumValue<Type> elementType = Type::I32;
    pjs::Ref<pjs::Array> elements;
  };

  //
  // Thrift::Map
  //

  class Map : public pjs::ObjectTemplate<Map> {
  public:
    pjs::EnumValue<Type> keyType = Type::I32;
    pjs::EnumValue<Type> valueType = Type::I32;
    pjs::Ref<pjs::Array> pairs;
  };

  //
  // Thrift::Message
  //

  class Message : public pjs::ObjectTemplate<Message> {
  public:

    //
    // Thrift::Message::Type
    //

    enum class Type {
      call = 1,
      reply = 2,
      exception = 3,
      oneway = 4,
    };

    pjs::EnumValue<Protocol> protocol;
    pjs::EnumValue<Type> type = Type::call;
    int seqID = 0;
    pjs::Ref<pjs::Str> name;
    pjs::Ref<pjs::Array> fields;

  private:
    Message(Protocol p = Protocol::compact) : protocol(p) {}

    friend class pjs::ObjectTemplate<Message>;
  };

  //
  // Thrift::Parser
  //

  class Parser : protected Deframer {
  public:
    Parser();

    void reset();
    void parse(Data &data);

  protected:
    virtual void on_message_start() {}
    virtual void on_message_end(Message *msg) = 0;

  private:
    enum State {
      ERROR = -1,
      START = 0,
      MESSAGE_HEAD,
      MESSAGE_NAME_LEN,
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
      LIST_SIZE,
      SET_HEAD,
      SET_SIZE,
      MAP_HEAD,
      MAP_TYPE,
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
      Type field_type;
      State element_types[2];
      int element_sizes[2];
      int size;
      int index;
      pjs::Value key;
      pjs::Ref<pjs::Object> obj;
    };

    uint8_t m_read_buf[16];
    pjs::Ref<Data> m_read_data;
    pjs::Ref<Message> m_message;
    Protocol m_protocol;
    Level* m_stack = nullptr;
    uint64_t m_var_int = 0;
    int m_element_type_code = 0;
    Type m_field_type;
    bool m_field_bool = false;

    virtual auto on_state(int state, int c) -> int override;

    bool var_int(int c);
    auto zigzag_to_int(uint32_t i) -> int32_t;
    auto zigzag_to_int(uint64_t i) -> int64_t;

    bool set_message_type(int type);
    auto set_field_type(int code) -> State;
    void set_value_type(int code, Type &type, State &state, int &read_size);
    auto set_value_start() -> State;
    auto set_value_end() -> State;
    void set_value(const pjs::Value &v);
    auto push_struct() -> State;
    auto push_list(int code, bool is_set, int size) -> State;
    auto push_map(int code_k, int code_v, int size) -> State;
    auto pop() -> State;

    void start();
    void end();
  };

  //
  // Thrift::StreamParser
  //

  class StreamParser : public Parser {
  public:
    StreamParser(const std::function<void(Message*)> &cb)
      : m_cb(cb) {}

    virtual void on_message_end(Message *msg) override {
      m_cb(msg);
    }

  private:
    std::function<void(Message*)> m_cb;
  };

};

} // namespace pipy

#endif // API_THRIFT_HPP
