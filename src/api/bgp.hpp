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

#ifndef API_BGP_HPP
#define API_BGP_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"
#include "deframer.hpp"

namespace pipy {

class Netmask;

//
// BGP
//

class BGP : public pjs::ObjectTemplate<BGP> {
public:
  static auto decode(const Data &data) -> pjs::Array*;
  static void encode(pjs::Object *payload, Data &data);

  //
  // BGP::MessageType
  //

  enum class MessageType {
    OPEN = 1,
    UPDATE = 2,
    NOTIFICATION = 3,
    KEEPALIVE = 4,
  };

  //
  // BGP::PathAttribute
  //

  class PathAttribute : public pjs::ObjectTemplate<PathAttribute> {
  public:

    //
    // BGP::PathAttribute::TypeCode
    //

    enum class TypeCode {
      ORIGIN = 1,
      AS_PATH = 2,
      NEXT_HOP = 3,
      MULTI_EXIT_DISC = 4,
      LOCAL_PREF = 5,
      ATOMIC_AGGREGATE = 6,
      AGGREGATOR = 7,
    };

    pjs::Ref<pjs::Str> name;
    pjs::Value value;
    int code = 0;
    bool optional = false;
    bool transitive = false;
    bool partial = false;
  };

  //
  // BGP::Message
  //

  class Message : public pjs::ObjectTemplate<Message> {
  public:
    MessageType type = MessageType::KEEPALIVE;
    pjs::Ref<pjs::Object> body;
  };

  //
  // BGP::MessageOpen
  //

  class MessageOpen : public pjs::ObjectTemplate<MessageOpen> {
  public:
    int version = 4;
    int myAS = 0;
    int holdTime = 0;
    pjs::Ref<pjs::Str> identifier;
    pjs::Ref<pjs::Object> capabilities;
    pjs::Ref<pjs::Object> parameters;
  };

  //
  // BGP::MessageUpdate
  //

  class MessageUpdate : public pjs::ObjectTemplate<MessageUpdate> {
  public:
    pjs::Ref<pjs::Array> withdrawnRoutes;
    pjs::Ref<pjs::Array> pathAttributes;
    pjs::Ref<pjs::Array> destinations;
  };

  //
  // BGP::MessageNotification
  //

  class MessageNotification : public pjs::ObjectTemplate<MessageNotification> {
  public:
    int errorCode = 0;
    int errorSubcode = 0;
    pjs::Ref<Data> data;

  private:
    MessageNotification() {}
    MessageNotification(int code, int subcode)
      : errorCode(code)
      , errorSubcode(subcode) {}

    friend class pjs::ObjectTemplate<MessageNotification>;
  };

  //
  // BGP::Parser
  //

  class Parser : protected Deframer {
  public:
    Parser();

    void reset();
    void parse(Data &data);

  protected:
    virtual void on_message_start() {}
    virtual void on_message_end(pjs::Object *payload) = 0;
    virtual void on_message_error(MessageNotification *msg) {}

  private:
    virtual auto on_state(int state, int c) -> int override;

    enum State {
      ERROR = -1,
      START = 0,
      HEADER,
      BODY,
    };

    uint8_t m_header[19];
    pjs::Ref<Data> m_body;
    pjs::Ref<Message> m_message;

    void message_start();
    void message_end();

    bool parse_open(Data::Reader &r);
    bool parse_update(Data::Reader &r);
    bool parse_notification(Data::Reader &r);
    bool error(int code, int subcode);

    bool read(Data::Reader &r, Data &data, size_t size);
    bool read(Data::Reader &r, uint8_t *data, size_t size);
    bool read(Data::Reader &r, uint8_t &data);
    bool read(Data::Reader &r, uint16_t &data);
    bool read(Data::Reader &r, uint32_t &data);

    auto read_address_prefix(Data::Reader &r) -> Netmask*;
    auto read_path_attribute(Data::Reader &r) -> PathAttribute*;
  };

  //
  // BGP::StreamParser
  //

  class StreamParser : public Parser {
  public:
    StreamParser(const std::function<void(pjs::Object *)> &cb)
      : m_cb(cb) {}

    virtual void on_message_end(pjs::Object *payload) override {
      m_cb(payload);
    }

  private:
    std::function<void(pjs::Object *)> m_cb;
  };
};

} // namespace pipy

#endif // API_BGP_HPP
