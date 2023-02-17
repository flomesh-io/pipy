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

//
// BGP
//

class BGP : public pjs::ObjectTemplate<BGP> {
public:
  static auto decode(const Data &data) -> pjs::Array*;
  static void encode(pjs::Object *payload, Data &data);

  //
  // MessageType
  //

  enum class MessageType {
    OPEN = 1,
    UPDATE = 2,
    NOTIFICATION = 3,
    KEEPALIVE = 4,
  };

  //
  // PathAttribute
  //

  class PathAttribute : public pjs::ObjectTemplate<PathAttribute> {
  public:
    pjs::Ref<pjs::Str> name;
    pjs::Value value;
    int code;
    bool optional;
    bool transitive;
    bool partial;
  };

  //
  // Message
  //

  class Message : public pjs::ObjectTemplate<Message> {
  public:
    MessageType type = MessageType::KEEPALIVE;
    pjs::Ref<pjs::Object> body;
  };

  //
  // MessageOpen
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
  // MessageUpdate
  //

  class MessageUpdate : public pjs::ObjectTemplate<MessageUpdate> {
  public:
    pjs::Ref<pjs::Array> withdrawnRoutes;
    pjs::Ref<pjs::Array> pathAttributes;
    pjs::Ref<pjs::Array> destinations;
  };

  //
  // MessageNotification
  //

  class MessageNotification : public pjs::ObjectTemplate<MessageNotification> {
  public:
    int errorCode;
    int errorSubcode;
    pjs::Ref<Data> data;
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

  private:
    virtual auto on_state(int state, int c) -> int override;

    pjs::Ref<Data> m_payload;
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
